// GENERATED from console-ui — edit there, then run `npm run sync`.
import { useState, type KeyboardEvent as ReactKeyboardEvent, type ReactNode } from "react";
import { Knob } from "../Knob";
import { Meter } from "../Meter";
import { Scope } from "../Scope";
import { Slider } from "../Slider";
import { Group, Key, Lcd, Seg } from "../Surface";
import { slicesRenderer, type SliceRegion } from "../renderers";
import { system } from "../tokens";
import type { ParamMap, ParamStore } from "../store";
import {
  Grid,
  PChoice,
  PCycle,
  PKnob,
  PSlider,
  PToggle,
  PanelScope,
  Readout,
  Row,
  useHas,
  type Schema,
  type SpecOverrides,
} from "./controls";
import { BANKS, Pads, padLabel, type PadInfo } from "./Pads";
import { Overlay, PluginFrame } from "./PluginFrame";

/* ------------------------------------------------------------------ types */

export type ChopMode = "threshold" | "regions" | "bpm" | "manual";
export interface ChopSettings {
  mode: ChopMode;
  threshold: number;
  regions: number;
  bpm: number;
  sliceBeats: number;
  minLengthMs: number;
  snapToZero: boolean;
}
export interface TrimSettings {
  start: number;
  end: number;
  fadeInMs: number;
  fadeOutMs: number;
  gainDb: number;
  stretchRatio: number;
}
export interface LayerInfo {
  hasSample: boolean;
  name: string;
  velStart: number;
  velEnd: number;
}

export interface SamplerState {
  /** Real kit/program name; omit when there is none (no fake LCD text). */
  kitName?: string;
  bpm: number;
  pads: PadInfo[];
  bank: number;
  selected: number;
  /** Selected pad's sample (selected layer). */
  /** seconds / sampleRate are the FILE's, from the host; omit when unknown. */
  sample?: { name: string; seconds?: number; sampleRate?: number; peaks?: number[] };
  /** False until the slices have been put on pads: auditioning is only real then. */
  slicesOnPads?: boolean;
  /** Current chop preview, normalised 0..1 of the sample. */
  slices: SliceRegion[];
  chop: ChopSettings;
  trim: TrimSettings;
  layer: number;
  layers: LayerInfo[];
  /** [pad][step] velocity 0..1 (0 = off). */
  pattern: number[][];
  patternLength: number;
  patternIndex: number;
  patternCount: number;
  step: number;
  playing: boolean;
  recording: boolean;
  swing: number;
  /** Time-correct steps per beat: 2, 3, 4, 6, 8, 12. */
  grid: number;
  chainMode: boolean;
  /** Pattern chain (pattern indices, played in order when chain mode is on). */
  chain?: number[];
  /** Velocity new steps are written with, 0..1. */
  stepVelocity?: number;
  /** Record quantize in steps of the grid; 0 = off (timing kept as nudge). */
  recordQuantize?: number;
  /** Swing grid: 8 (1/8) or 16 (1/16). */
  swingDivision?: number;
  /** Time signature numerator (bar = grid beats; denominator is the quarter note). */
  beatsPerBar?: number;
  /** Muted rows (pads): the row keeps its steps but the sequencer skips it. */
  rowMutes?: number[];
  /** Repeat count per chain entry (song mode); parallel to `chain`. */
  chainRepeats?: number[];
  /** Per-step Shift Timing in ticks (960 PPQ), keyed "pad:step"; absent = 0. */
  stepNudges?: Record<string, number>;
  /** Gate lengths in ticks that differ from one step, keyed "pad:step". */
  gateLengths?: Record<string, number>;
  /** Step probability in percent, keyed "pad:step"; absent = 100. */
  stepChances?: Record<string, number>;
  /** Ratchet count per step, keyed "pad:step"; absent = 1. */
  stepRatchets?: Record<string, number>;
  /** False when the engine has nothing to undo (Undo keys disable). */
  canUndo?: boolean;
}

export interface SampleLibrary {
  folders: string[];
  files: { name: string; path: string; folder: number }[];
  /** Load a library file onto the selected pad and layer. */
  load(path: string): void;
  addFolder(): void;
  removeFolder(index: number): void;
  refresh(): void;
}

/** A titled row of file actions (kit, MPC program, MIDI ...). */
export interface FileActionGroup {
  title: string;
  actions: { label: string; run(): void }[];
}

export interface PerformSettings {
  on: boolean;
  /** 0 Chromatic, 1 Scale notes, 2 Chords (diatonic), 3 Chromatic chords, 4 Progression. */
  mode: number;
  /** 0 Triads, 1 Sevenths, 2 Ninths (Chords mode). */
  chordType: number;
  octave: number;
  /** Scale id; "" follows the composition. */
  scale: string;
  /** Base pitch class 0..11; -1 follows the composition key. */
  basePc: number;
  chromaticChord: string;
  constrain: boolean;
  /** 0 banks continue the mapping, 1 every bank restarts it. */
  bankBehaviour: number;
  /** 1..127 */
  velocity: number;
}

export interface PerformHost {
  settings: PerformSettings;
  scales: { id: string; name: string }[];
  chromaticChords: readonly string[];
  progressions: { name: string; chords: string[] }[];
  progressionIndex: number;
  set(patch: Partial<PerformSettings>): void;
  /** Store the current settings as the default. */
  save(): void;
  selectProgression(index: number): void;
  importProgression?(): void;
  progressionFromMidi?(): void;
  exportProgression?(): void;
}

export type TrimOp = "normalize" | "reverse" | "removeDc";

export interface SamplerHost {
  trigger(pad: number, velocity: number): void;
  /** Pad let go: ends Gate-mode voices and held note repeat. */
  release?(pad: number): void;
  setSwingDivision?(division: number): void;
  /** Shift Timing for one step, ticks at 960 PPQ (clamped to half a step by the engine). */
  setStepNudge?(pad: number, step: number, ticks: number): void;
  /** Gate length for one step, ticks; 0 = one step. Only Gate-mode pads use it. */
  setStepLength?(pad: number, step: number, ticks: number): void;
  /** Chance (percent, 0-100) this step sounds on a pass of the pattern. */
  setStepProbability?(pad: number, step: number, percent: number): void;
  /** Repeat the step N times (1-8) across its own length. */
  setStepRatchet?(pad: number, step: number, count: number): void;
  selectPad(pad: number): void;
  selectBank(bank: number): void;
  setChop(patch: Partial<ChopSettings>): void;
  /** Chop the selected pad's sample and put slice N on pad firstPad+N. */
  assignSlices(firstPad: number): void;
  setTrim(patch: Partial<TrimSettings>): void;
  /** Apply the trim window, fades, gain and stretch destructively. */
  applyTrim(): void;
  trimOp(op: TrimOp): void;
  undo(): void;
  selectLayer(layer: number): void;
  toggleStep(pad: number, step: number): void;
  transport(cmd: "play" | "stop" | "record"): void;
  setSwing(amount: number): void;
  setGrid(stepsPerBeat: number): void;
  setPatternLength(steps: number): void;
  selectPattern(index: number): void;
  loadSample(): void;
  clearPattern(): void;
  setChainMode(on: boolean): void;
  /** Time signature numerator for the current pattern. */
  setBeatsPerBar?(beats: number): void;
  /** Mute/unmute a step row (pad). */
  setRowMute?(pad: number, muted: boolean): void;
  /** Repeat count per chain entry (song mode). */
  setChainRepeats?(repeats: number[]): void;
  /** Play one slice of the current chop preview. */
  auditionSlice(index: number): void;
  /** Manual chop: drop a slice marker at a 0..1 position. Manual mode is only
   *  offered when the host implements this. */
  placeMarker?(position: number): void;
  clearMarkers?(): void;
  /** Audio files dropped on a pad: first file on that pad, the rest on the next pads. */
  dropFiles?(pad: number, files: File[]): void;
  clearPad?(pad: number): void;
  setStepVelocity?(velocity: number): void;
  setRecordQuantize?(steps: number): void;
  setChain?(chain: number[]): void;
  library?: SampleLibrary;
  fileActions?: FileActionGroup[];
  perform?: PerformHost;
  /** Optional engine features: their controls only appear when present. */
  noteRepeat?: (on: boolean, stepsPerBeat: number) => void;
  input?: {
    /** threshold: 0..1 over −60..0 dB; 0 starts recording as soon as it is armed. */
    armed: boolean;
    recording: boolean;
    source: number;
    sources: string[];
    threshold: number;
    monitor: boolean;
    level: () => [number, number];
    setSource(i: number): void;
    setThreshold(v: number): void;
    setMonitor(on: boolean): void;
    arm(on: boolean): void;
    stop(): void;
    /** False while the device has no input open (only resampling works); `enable` opens it. */
    enabled?: boolean;
    enable?(): void;
    /** Length of the take so far, and the most one take can hold. */
    seconds?: number;
    maxSeconds?: number;
    /** Where the take lands, e.g. "A01 · L1". */
    target?: string;
  };
}

export interface SamplerPanelProps {
  state: SamplerState;
  host: SamplerHost;
  /** Selected pad's program parameters (bound like any Console panel). */
  program: { store: ParamStore; map: ParamMap; specs?: SpecOverrides };
  /** Playhead of the selected pad's sample, 0..1, read per frame. */
  playhead?: () => number | undefined;
  initialMode?: SamplerMode;
  /** Program deck page to open on (previews and tests). */
  initialProgramPage?: "sound" | "map";
}

/* --------------------------------------------------------- program schema */

export const PAD_FILTERS = ["Off", "LP 2-pole", "LP 4-pole", "BP", "HP", "Band-reject"] as const;
export const samplerProgramSchema = {
  volume: { label: "Level", def: 0.8, spec: { unit: "dB", min: -48, max: 6 } },
  pan: { label: "Pan", def: 0.5, bipolar: true, spec: { unit: "pan", min: -50, max: 50 } },
  tuneCoarse: { label: "Tune", def: 0.5, bipolar: true, spec: { unit: "st", min: -24, max: 24 } },
  tuneFine: { label: "Fine", def: 0.5, bipolar: true, spec: { unit: "ct", min: -100, max: 100 } },
  filterShape: { label: "Filter", def: 0, spec: { options: PAD_FILTERS } },
  filterCutoff: {
    label: "Cutoff",
    def: 1,
    spec: { unit: "Hz", min: 20, max: 20000, curve: "log" },
  },
  filterResonance: { label: "Reso", def: 0, spec: { unit: "%", min: 0, max: 100 } },
  filterEnvAmount: { label: "Env Amt", def: 0, spec: { unit: "%", min: 0, max: 100 } },
  ampAttackMs: { label: "Attack", def: 0, spec: { unit: "ms", min: 0, max: 2000 } },
  ampDecayMs: { label: "Decay", def: 0.3, spec: { unit: "ms", min: 1, max: 5000, curve: "log" } },
  ampSustain: { label: "Sustain", def: 1, spec: { unit: "%", min: 0, max: 100 } },
  ampReleaseMs: {
    label: "Release",
    def: 0.2,
    spec: { unit: "ms", min: 1, max: 5000, curve: "log" },
  },
  velocitySensitivity: { label: "Vel → Level", def: 1, spec: { unit: "%", min: 0, max: 100 } },
  velocityToFilter: { label: "Vel → Filter", def: 0, spec: { unit: "%", min: 0, max: 100 } },
  pitchEnvAmount: {
    label: "Pitch Env",
    def: 0.5,
    bipolar: true,
    spec: { unit: "st", min: -24, max: 24 },
  },
  pitchAttackMs: { label: "P Attack", def: 0, spec: { unit: "ms", min: 0, max: 2000 } },
  pitchDecayMs: {
    label: "P Decay",
    def: 0.3,
    spec: { unit: "ms", min: 1, max: 5000, curve: "log" },
  },
  lfoType: {
    label: "LFO",
    def: 0,
    spec: { options: ["Sine", "Triangle", "Saw", "Square", "S&H"] },
  },
  lfoRate: {
    label: "LFO Rate",
    def: 0.3,
    spec: { unit: "rate", min: 0.05, max: 30, curve: "log" },
  },
  lfoPitch: { label: "→ Pitch", def: 0.5, bipolar: true, spec: { unit: "%", min: -100, max: 100 } },
  lfoCutoff: {
    label: "→ Cutoff",
    def: 0.5,
    bipolar: true,
    spec: { unit: "%", min: -100, max: 100 },
  },
  lfoVolume: {
    label: "→ Level",
    def: 0.5,
    bipolar: true,
    spec: { unit: "%", min: -100, max: 100 },
  },
  triggerMode: { label: "Trigger", def: 0, spec: { options: ["One-shot", "Gate"] } },
  // The engine clamps mute groups to 1..16 (DrumMachineController).
  muteGroup: {
    label: "Mute group",
    def: 0,
    spec: { options: ["Off", ...Array.from({ length: 16 }, (_, i) => String(i + 1))] },
  },
  // Mapping page. Layer keys address the selected velocity layer.
  layerVolume: { label: "Layer level", def: 0.8, spec: { unit: "dB", min: -48, max: 6 } },
  layerPan: {
    label: "Layer pan",
    def: 0.5,
    bipolar: true,
    spec: { unit: "pan", min: -50, max: 50 },
  },
  velStart: { label: "Vel start", def: 0, spec: { min: 0, max: 127, decimals: 0 } },
  velEnd: { label: "Vel end", def: 1, spec: { min: 0, max: 127, decimals: 0 } },
  layerActive: { label: "Layer on", def: 1, spec: { options: ["Off", "On"] } },
  reverse: { label: "Reverse", def: 0, spec: { options: ["Off", "On"] } },
  // Layer loop. Start/end are positions in the layer's sample (0..100 %).
  loop: { label: "Loop", def: 0, spec: { options: ["Off", "On"] } },
  loopMode: { label: "Loop mode", def: 0, spec: { options: ["Forward", "Ping-pong"] } },
  loopStart: { label: "Loop start", def: 0, spec: { unit: "%", min: 0, max: 100, decimals: 1 } },
  loopEnd: { label: "Loop end", def: 1, spec: { unit: "%", min: 0, max: 100, decimals: 1 } },
  loopCrossfade: { label: "Loop X-fade", def: 0, spec: { unit: "ms", min: 0, max: 500 } },
  padNote: {
    label: "Pad note",
    def: 36 / 127,
    spec: { unit: "note", min: 0, max: 127, decimals: 0 },
  },
  // The drum mixer has 24 channels (cs::kDrumChannelCount); hosts override via specs.
  outputChannel: {
    label: "Output",
    def: 0,
    spec: { options: Array.from({ length: 24 }, (_, i) => `Ch ${i + 1}`) },
  },
  polyphony: {
    label: "Voices",
    def: 0,
    spec: { options: ["Auto", ...Array.from({ length: 16 }, (_, i) => String(i + 1))] },
  },
  mono: { label: "Mono", def: 0, spec: { options: ["Off", "On"] } },
  mute: { label: "Mute", def: 0, spec: { options: ["Off", "On"] } },
  solo: { label: "Solo", def: 0, spec: { options: ["Off", "On"] } },
} as const satisfies Schema;

/* ------------------------------------------------------------------ modes */

export type SamplerMode =
  "record" | "trim" | "chop" | "program" | "sequence" | "browse" | "perform";
type Mode = SamplerMode;
const MODES: { id: Mode; label: string; n: string }[] = [
  { id: "record", label: "Record", n: "1" },
  { id: "trim", label: "Trim", n: "2" },
  { id: "chop", label: "Chop", n: "3" },
  { id: "program", label: "Program", n: "4" },
  { id: "sequence", label: "Sequence", n: "5" },
  { id: "browse", label: "Browse", n: "6" },
  { id: "perform", label: "Perform", n: "7" },
];
const PERFORM_MODES = ["Chromatic", "Scale notes", "Chords", "Chr. chords", "Progression"];
const NOTE_NAMES = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"];
const GRIDS = [
  { n: 2, l: "1/8" },
  { n: 3, l: "1/8T" },
  { n: 4, l: "1/16" },
  { n: 6, l: "1/16T" },
  { n: 8, l: "1/32" },
  { n: 12, l: "1/32T" },
];
const fmtSec = (s: number) => (s < 10 ? s.toFixed(2) : s.toFixed(1));

/* ----------------------------------------------------------------- screen */

/** Step grid, MPC grid-view style: eight pad rows at a time (half a bank)
 *  so rows stay big enough to hit (26 px), with a 1–8 / 9–16 page switch. */
type StepPick = { pad: number; step: number } | null;
const PPQ = 960;

function SequenceGrid({
  state,
  host,
  pick,
  onPick,
}: {
  state: SamplerState;
  host: SamplerHost;
  pick?: StepPick;
  onPick?: (p: StepPick) => void;
}) {
  const len = state.patternLength;
  const selHalf = Math.floor((state.selected % 16) / 8);
  const [halfPick, setHalfPick] = useState<number | null>(null);
  const half = halfPick ?? selHalf;
  const base = state.bank * 16 + half * 8;
  const rows = Array.from({ length: 8 }, (_, i) => base + 7 - i);
  const bankName = BANKS[state.bank];
  return (
    <div
      className="cs-screen"
      style={{
        height: 320,
        boxSizing: "border-box",
        padding: "58px 12px 8px",
        overflow: "hidden",
        position: "relative",
      }}
    >
      <div style={{ position: "absolute", right: 12, top: 12 }}>
        <Seg
          label="Pad rows"
          value={String(half)}
          onChange={(h) => setHalfPick(Number(h))}
          options={[
            { id: "0", label: `${bankName}01–08` },
            { id: "1", label: `${bankName}09–16` },
          ]}
        />
      </div>
      <div
        role="grid"
        aria-label={`Pattern ${state.patternIndex + 1}, pads ${bankName}${half ? "09–16" : "01–08"}`}
        style={{
          display: "grid",
          gridTemplateColumns: `120px repeat(${len}, minmax(0, 1fr))`,
          gap: 3,
        }}
      >
        {rows.map((pad) => {
          const p = state.pads[pad];
          const sel = pad === state.selected;
          const muted = state.rowMutes?.includes(pad) ?? false;
          return [
            <button
              key={`l${pad}`}
              type="button"
              onClick={() => host.selectPad(pad)}
              className="cs-readout"
              data-grid-cell=""
              style={{
                height: 26,
                border: 0,
                borderRadius: 3,
                padding: "0 6px",
                textAlign: "left",
                cursor: "pointer",
                fontSize: 12,
                background: sel ? "rgba(255,138,0,.25)" : "transparent",
                color: muted ? "#5b5953" : p?.hasSample ? "#ffae3a" : "#8f8c85",
                textDecoration: muted ? "line-through" : "none",
                whiteSpace: "nowrap",
                overflow: "hidden",
                textOverflow: "ellipsis",
              }}
            >
              {muted ? "M " : ""}
              {padLabel(pad)} {p?.hasSample ? p.name : ""}
            </button>,
            ...Array.from({ length: len }, (_, s) => {
              const v = state.pattern[pad]?.[s] ?? 0;
              const beat = s % Math.max(1, state.grid || 4) === 0;
              const now = state.playing && s === state.step;
              const picked = pick?.pad === pad && pick.step === s;
              return (
                <button
                  key={`${pad}-${s}`}
                  type="button"
                  role="gridcell"
                  data-grid-cell=""
                  aria-label={`${padLabel(pad)} step ${s + 1}${v ? `, velocity ${Math.round(v * 127)}` : ""}${muted ? ", row muted" : ""}`}
                  aria-pressed={v > 0}
                  onClick={(e) => {
                    if (e.shiftKey && onPick && v > 0) {
                      onPick({ pad, step: s });
                      return;
                    }
                    host.toggleStep(pad, s);
                  }}
                  onContextMenu={
                    onPick
                      ? (e) => {
                          e.preventDefault();
                          if (v > 0) onPick({ pad, step: s });
                        }
                      : undefined
                  }
                  style={{
                    height: 26,
                    padding: 0,
                    border: 0,
                    borderRadius: 3,
                    cursor: "pointer",
                    opacity: muted ? 0.35 : 1,
                    background:
                      v > 0
                        ? `rgba(255,${120 + Math.round(v * 54)},${Math.round(v * 58)},${0.45 + v * 0.55})`
                        : now
                          ? "rgba(255,243,220,.18)"
                          : beat
                            ? "rgba(255,174,58,.12)"
                            : "rgba(255,174,58,.05)",
                    boxShadow:
                      v > 0 ? `0 0 ${4 + v * 6}px rgba(255,138,0,${0.3 + v * 0.4})` : "none",
                    outline: picked
                      ? "2px solid #3cd6dd"
                      : now
                        ? "1px solid rgba(255,243,220,.6)"
                        : "none",
                  }}
                />
              );
            }),
          ];
        })}
      </div>
    </div>
  );
}

function SampleScreen({
  state,
  mode,
  playhead,
  host,
}: {
  state: SamplerState;
  mode: Mode;
  playhead?: () => number | undefined;
  host: SamplerHost;
}) {
  const c = state.chop;
  const slices = mode === "chop" ? state.slices : mode === "trim" ? [] : state.slices;
  const selSlice = state.pads[state.selected]?.slice;
  const beats =
    mode === "chop" && c.mode === "bpm" && state.sample?.seconds !== undefined
      ? Math.round((state.sample.seconds * c.bpm) / 60)
      : 0;
  const manual =
    mode === "chop" &&
    c.mode === "manual" &&
    host.placeMarker !== undefined &&
    state.sample !== undefined;
  const scope = (
    <Scope
      renderer={slicesRenderer}
      width={1288}
      height={320}
      label={
        state.sample
          ? `Waveform of ${state.sample.name}, ${slices.length} slices`
          : "No sample on this pad"
      }
      getFrame={playhead ? () => ({ playhead: playhead() }) : undefined}
      params={{
        peaks: state.sample?.peaks,
        slices,
        selected: mode === "chop" ? undefined : selSlice,
        threshold: mode === "chop" && c.mode === "threshold" ? c.threshold : undefined,
        beats,
        trim: mode === "trim" ? { start: state.trim.start, end: state.trim.end } : undefined,
        top: 52,
      }}
    />
  );
  if (!manual) return scope;
  return (
    <div
      style={{ cursor: "crosshair" }}
      onClick={(e) => {
        const r = e.currentTarget.getBoundingClientRect();
        host.placeMarker!(Math.min(1, Math.max(0, (e.clientX - r.left) / r.width)));
      }}
    >
      {scope}
    </div>
  );
}

function RecordScreen({ host }: { host: SamplerHost }) {
  const inp = host.input!;
  return (
    <div
      className="cs-screen"
      style={{
        height: 320,
        boxSizing: "border-box",
        padding: "56px 28px 20px",
        display: "flex",
        gap: 28,
        alignItems: "stretch",
      }}
    >
      <div
        style={{
          display: "flex",
          flexDirection: "column",
          justifyContent: "center",
          gap: 10,
          flexGrow: 1,
        }}
      >
        <Readout
          size={46}
          style={{ lineHeight: "48px", color: inp.recording ? "#ff5a4a" : undefined }}
        >
          {inp.recording
            ? `● REC ${fmtSec(inp.seconds ?? 0)} s`
            : inp.armed
              ? "ARMED · WAITING"
              : "READY"}
        </Readout>
        <Readout size={13} style={{ opacity: 0.8, letterSpacing: ".12em" }}>
          SOURCE {inp.sources[inp.source]?.toUpperCase()} ·{" "}
          {inp.threshold <= 0
            ? "STARTS ON ARM"
            : `STARTS ABOVE ${Math.round(-60 + inp.threshold * 60)} dB`}
          {inp.target ? ` · INTO ${inp.target}` : ""}
          {inp.maxSeconds ? ` · MAX ${Math.round(inp.maxSeconds)} s` : ""}
        </Readout>
        {inp.enabled === false && (
          <Readout size={13} style={{ opacity: 0.7 }}>
            AUDIO INPUT OFF · RESAMPLE ONLY
          </Readout>
        )}
        <div style={{ position: "relative", height: 34, marginTop: 10 }}>
          <div style={{ display: "flex", flexDirection: "column", gap: 4 }}>
            <Meter read={() => inp.level()[0]} segments={48} thickness={14} label="Input left" />
            <Meter read={() => inp.level()[1]} segments={48} thickness={14} label="Input right" />
          </div>
          <span
            aria-hidden="true"
            style={{
              position: "absolute",
              top: -6,
              bottom: -6,
              left: `${inp.threshold * 100}%`,
              width: 2,
              background: "#3cd6dd",
              boxShadow: "0 0 8px #3cd6dd",
            }}
          />
        </div>
      </div>
    </div>
  );
}

/** Sample library browser on the screen: click a file to load it onto the
 *  selected pad and layer (MPC Browser → load to pad). */
function LibraryScreen({ lib, target }: { lib: SampleLibrary; target: string }) {
  const [filter, setFilter] = useState("");
  const q = filter.trim().toLowerCase();
  const files = q ? lib.files.filter((f) => f.name.toLowerCase().includes(q)) : lib.files;
  const short = (f: string) => f.split(/[\\/]/).pop() || f;
  return (
    <div
      className="cs-screen"
      style={{
        height: 320,
        boxSizing: "border-box",
        padding: "58px 16px 12px",
        display: "flex",
        flexDirection: "column",
        gap: 8,
      }}
    >
      <div
        style={{ display: "flex", gap: 8, alignItems: "center", flexWrap: "nowrap", minWidth: 0 }}
      >
        <input
          className="cs-lcd"
          aria-label="Filter samples"
          placeholder="Filter"
          value={filter}
          onChange={(e) => setFilter(e.target.value)}
          style={{
            height: 36,
            width: 200,
            boxSizing: "border-box",
            padding: "0 12px",
            fontSize: 13,
            border: 0,
            outline: "none",
          }}
        />
        <div
          role="group"
          aria-label="Library folders"
          style={{ display: "flex", gap: 8, overflow: "hidden", flexGrow: 1, minWidth: 0 }}
        >
          {lib.folders.map((f, i) => (
            <Key
              key={f}
              label={`Remove folder ${short(f)}`}
              onClick={() => lib.removeFolder(i)}
              style={{ maxWidth: 190, flexShrink: 0 }}
            >
              <span style={{ overflow: "hidden", textOverflow: "ellipsis" }}>{short(f)}</span>
              &nbsp;×
            </Key>
          ))}
        </div>
        <Key onClick={lib.addFolder}>+ Folder</Key>
        <Key onClick={lib.refresh}>Rescan</Key>
        <Readout size={13} style={{ whiteSpace: "nowrap" }}>
          → {target}
        </Readout>
      </div>
      <div
        role="list"
        aria-label="Samples"
        style={{
          flexGrow: 1,
          minHeight: 0,
          overflowY: "auto",
          display: "grid",
          gridTemplateColumns: "repeat(4, minmax(0, 1fr))",
          gap: 8,
          alignContent: "start",
        }}
      >
        {lib.folders.length === 0 ? (
          <Readout size={13} style={{ gridColumn: "1 / -1", opacity: 0.7 }}>
            NO FOLDERS · ADD ONE TO BROWSE ONE-SHOTS
          </Readout>
        ) : files.length === 0 ? (
          <Readout size={13} style={{ gridColumn: "1 / -1", opacity: 0.7 }}>
            {q ? "NO MATCHES" : "NO AUDIO FILES IN THESE FOLDERS"}
          </Readout>
        ) : (
          files.slice(0, 400).map((f) => (
            <button
              key={f.path}
              type="button"
              role="listitem"
              className="cs-lcd"
              title={f.path}
              onClick={() => lib.load(f.path)}
              style={{
                minHeight: 36,
                padding: "0 10px",
                fontSize: 12,
                cursor: "pointer",
                whiteSpace: "nowrap",
                overflow: "hidden",
                textOverflow: "ellipsis",
                display: "block",
                textAlign: "left",
                border: 0,
              }}
            >
              {f.name}
            </button>
          ))
        )}
      </div>
    </div>
  );
}

function PerformScreen({ perform }: { perform: PerformHost }) {
  const p = perform.settings;
  const prog = perform.progressions[perform.progressionIndex];
  const scale = p.scale
    ? (perform.scales.find((x) => x.id === p.scale)?.name ?? p.scale)
    : "Song scale";
  const base = p.basePc < 0 ? "Song key" : NOTE_NAMES[p.basePc];
  const detail =
    p.mode === 4
      ? prog
        ? prog.chords.join(" · ") || "No chords"
        : "No progression loaded"
      : [
          p.mode === 2
            ? ["Triads", "Sevenths", "Ninths"][p.chordType]
            : p.mode === 3
              ? p.chromaticChord
              : "",
          `${base} · ${scale}`,
          `Oct ${p.octave > 0 ? "+" : ""}${p.octave}`,
          p.constrain ? "In scale" : "",
        ]
          .filter(Boolean)
          .join(" · ");
  return (
    <div
      className="cs-screen"
      style={{
        height: 320,
        boxSizing: "border-box",
        padding: "70px 28px 20px",
        display: "flex",
        flexDirection: "column",
        gap: 12,
      }}
    >
      <Readout size={46} style={{ lineHeight: "48px", color: p.on ? undefined : "#8f8c85" }}>
        PAD PERFORM {p.on ? "ON" : "OFF"}
      </Readout>
      <Readout size={18} style={{ letterSpacing: ".12em" }}>
        {PERFORM_MODES[p.mode]?.toUpperCase()}
        {p.mode === 4 && prog ? ` · ${prog.name.toUpperCase()}` : ""}
      </Readout>
      <Readout
        size={14}
        style={{ opacity: 0.8, overflow: "hidden", textOverflow: "ellipsis", whiteSpace: "nowrap" }}
      >
        {detail}
      </Readout>
      <Readout size={12} style={{ opacity: 0.55 }}>
        VELOCITY {p.velocity} · BANKS {p.bankBehaviour ? "RESTART" : "CONTINUE"}
      </Readout>
    </div>
  );
}

/* ------------------------------------------------------------------- deck */

function Ctx({
  title,
  chip,
  children,
  extra,
}: {
  title: string;
  chip: string;
  children: ReactNode;
  extra?: ReactNode;
}) {
  return (
    <Group title={title} chip={chip} extra={extra} style={{ flexGrow: 1, minWidth: 0 }}>
      {children}
    </Group>
  );
}

function ChopDeck({ state, host }: { state: SamplerState; host: SamplerHost }) {
  const c = state.chop;
  const n = state.slices.length;
  const first = state.selected + 1;
  const secs = state.sample?.seconds;
  const canAudition = state.slicesOnPads !== false;
  const chopModes = [
    { id: "threshold" as const, label: "Threshold" },
    { id: "regions" as const, label: "Regions" },
    { id: "bpm" as const, label: "BPM" },
    ...(host.placeMarker ? [{ id: "manual" as const, label: "Manual" }] : []),
  ];
  const main =
    c.mode === "threshold" ? (
      <Knob
        label="Threshold"
        size="xl"
        showValue
        value={c.threshold}
        defaultValue={0.5}
        onChange={(v) => host.setChop({ threshold: v })}
        spec={{ unit: "%", min: 0, max: 100 }}
      />
    ) : c.mode === "regions" ? (
      <Knob
        label="Slices"
        size="xl"
        showValue
        value={(c.regions - 1) / 63}
        defaultValue={15 / 63}
        onChange={(v) => host.setChop({ regions: 1 + Math.round(v * 63) })}
        spec={{ min: 1, max: 64 }}
      />
    ) : c.mode === "bpm" ? (
      <Knob
        label="Loop BPM"
        size="xl"
        showValue
        value={(c.bpm - 40) / 200}
        defaultValue={0.25}
        onChange={(v) => host.setChop({ bpm: Math.round(40 + v * 200) })}
        spec={{ unit: "bpm", min: 40, max: 240, decimals: 0 }}
      />
    ) : (
      <div style={{ display: "flex", flexDirection: "column", gap: 8, width: 170 }}>
        <Lcd style={{ height: 64, fontSize: 13, justifyContent: "center", textAlign: "center" }}>
          Click the waveform to drop a marker
        </Lcd>
        {host.clearMarkers && (
          <Key onClick={host.clearMarkers} style={{ minHeight: 38 }}>
            Clear markers
          </Key>
        )}
      </div>
    );
  return (
    <Ctx title="Chop" chip={system.source} extra={<Readout size={13}>{n} SLICES</Readout>}>
      <Seg
        label="Chop mode"
        value={c.mode}
        onChange={(m) => host.setChop({ mode: m })}
        options={chopModes}
      />
      <Row align="flex-end" justify="space-between">
        {main}
        {c.mode === "bpm" && (
          <Knob
            label="Slice"
            size="l"
            value={[0.25, 0.5, 1, 2, 4].indexOf(c.sliceBeats) / 4}
            defaultValue={0.5}
            onChange={(v) => host.setChop({ sliceBeats: [0.25, 0.5, 1, 2, 4][Math.round(v * 4)] })}
            spec={{ options: ["1/16", "1/8", "1 beat", "2 beats", "1 bar"] }}
          />
        )}
        <Knob
          label="Min length"
          size="l"
          value={c.minLengthMs / 500}
          defaultValue={0.1}
          onChange={(v) => host.setChop({ minLengthMs: Math.round(v * 500) })}
          spec={{ unit: "ms", min: 0, max: 500 }}
        />
        <div style={{ display: "flex", flexDirection: "column", gap: 8, width: 190 }}>
          <Key
            pressed={c.snapToZero}
            onClick={() => host.setChop({ snapToZero: !c.snapToZero })}
            style={{ minHeight: 38 }}
          >
            Snap to zero
          </Key>
          <Lcd style={{ height: 38, fontSize: 13 }}>
            {n
              ? `→ ${padLabel(state.selected)}…${padLabel(Math.min(127, state.selected + n - 1))}`
              : "No slices yet"}
          </Lcd>
        </div>
      </Row>
      <div
        role="group"
        aria-label="Audition slices"
        style={{ display: "flex", flexWrap: "wrap", gap: 8 }}
      >
        {state.slices.slice(0, 32).map((sl, i) => (
          <button
            key={i}
            type="button"
            className="cs-lcd"
            onClick={() => host.auditionSlice(i)}
            disabled={!canAudition}
            aria-label={
              canAudition
                ? `Play slice ${i + 1}`
                : `Slice ${i + 1} (put the slices on pads to audition)`
            }
            title={canAudition ? undefined : "Chop → pads first: slices become pads you can play"}
            style={{
              display: "inline-flex",
              alignItems: "center",
              minHeight: 36,
              minWidth: 64,
              padding: "0 10px",
              fontSize: 12,
              cursor: canAudition ? "pointer" : "default",
              gap: 6,
              opacity: canAudition ? 1 : 0.55,
            }}
          >
            {String(i + 1).padStart(2, "0")}
            {secs !== undefined && (
              <span style={{ opacity: 0.6 }}>{fmtSec((sl.end - sl.start) * secs)}s</span>
            )}
          </button>
        ))}
      </div>
      <Grid cols={2} gap={8}>
        <Key
          onClick={() => host.assignSlices(first - 1)}
          disabled={!n}
          style={{ minHeight: 48, fontSize: 14 }}
        >
          Chop → pads
        </Key>
        <Key
          onClick={() => host.undo()}
          disabled={state.canUndo === false}
          style={{ minHeight: 48 }}
        >
          Undo
        </Key>
      </Grid>
    </Ctx>
  );
}

function TrimDeck({ state, host }: { state: SamplerState; host: SamplerHost }) {
  const t = state.trim;
  const secs = state.sample?.seconds ?? 1;
  return (
    <Ctx
      title="Trim · Edit"
      chip={system.source}
      extra={<Readout size={13}>{fmtSec((t.end - t.start) * secs)} s KEPT</Readout>}
    >
      <Grid cols={2} gap={16}>
        <Slider
          label="Start"
          width={330}
          value={t.start}
          defaultValue={0}
          onChange={(v) => host.setTrim({ start: Math.min(v, t.end - 0.001) })}
          spec={{ unit: "s", min: 0, max: secs, decimals: 3 }}
          accent="#3cd6dd"
        />
        <Slider
          label="End"
          width={330}
          value={t.end}
          defaultValue={1}
          onChange={(v) => host.setTrim({ end: Math.max(v, t.start + 0.001) })}
          spec={{ unit: "s", min: 0, max: secs, decimals: 3 }}
          accent="#3cd6dd"
        />
      </Grid>
      <Row justify="space-between">
        <Knob
          label="Fade in"
          size="l"
          value={t.fadeInMs / 500}
          defaultValue={0}
          onChange={(v) => host.setTrim({ fadeInMs: Math.round(v * 500) })}
          spec={{ unit: "ms", min: 0, max: 500 }}
        />
        <Knob
          label="Fade out"
          size="l"
          value={t.fadeOutMs / 500}
          defaultValue={0}
          onChange={(v) => host.setTrim({ fadeOutMs: Math.round(v * 500) })}
          spec={{ unit: "ms", min: 0, max: 500 }}
        />
        <Knob
          label="Gain"
          size="l"
          bipolar
          value={(t.gainDb + 24) / 48}
          defaultValue={0.5}
          onChange={(v) => host.setTrim({ gainDb: v * 48 - 24 })}
          spec={{ unit: "dB", min: -24, max: 24 }}
        />
        <Knob
          label="Stretch"
          size="l"
          value={Math.log2(t.stretchRatio) / 2 + 0.5}
          defaultValue={0.5}
          onChange={(v) => host.setTrim({ stretchRatio: Math.pow(2, (v - 0.5) * 2) })}
          spec={{ unit: "x", min: 0.5, max: 2, curve: "log" }}
        />
      </Row>
      <div style={{ display: "grid", gridTemplateColumns: "repeat(5, minmax(0, 1fr))", gap: 8 }}>
        <Key onClick={() => host.trimOp("normalize")} style={{ minHeight: 48 }}>
          Normalize
        </Key>
        <Key onClick={() => host.trimOp("reverse")} style={{ minHeight: 48 }}>
          Reverse
        </Key>
        <Key onClick={() => host.trimOp("removeDc")} style={{ minHeight: 48 }}>
          Remove DC
        </Key>
        <Key onClick={() => host.applyTrim()} style={{ minHeight: 48 }}>
          Apply trim
        </Key>
        <Key
          onClick={() => host.undo()}
          disabled={state.canUndo === false}
          style={{ minHeight: 48 }}
        >
          Undo
        </Key>
      </div>
    </Ctx>
  );
}

const Divider = () => (
  <div
    style={{
      width: 1,
      alignSelf: "stretch",
      background: "#0a0a0b",
      boxShadow: "1px 0 0 rgba(255,255,255,.04)",
    }}
  />
);

/** Velocity-layer overlap, as the engine resolves it: the lowest-numbered
 *  active layer whose range contains the velocity wins. */
function overlapNote(layers: LayerInfo[], i: number): string | null {
  const l = layers[i];
  if (!l?.hasSample) return null;
  const hit = layers.findIndex(
    (o, j) => j < i && o.hasSample && o.velStart <= l.velEnd && l.velStart <= o.velEnd,
  );
  return hit < 0
    ? null
    : `Overlaps L${hit + 1} (${layers[hit].velStart}–${layers[hit].velEnd}): L${hit + 1} wins there`;
}

function MappingPage({ state, host }: { state: SamplerState; host: SamplerHost }) {
  const note = overlapNote(state.layers, state.layer);
  const hasLayer = useHas(
    "layerVolume",
    "layerPan",
    "velStart",
    "velEnd",
    "layerActive",
    "reverse",
  );
  const hasPad =
    useHas("padNote", "outputChannel", "polyphony", "mono", "mute", "solo") || !!host.clearPad;
  const hasLoop = useHas("loop", "loopStart", "loopEnd");
  return (
    <>
      {hasLayer && (
        <>
          <span className="cs-sub">Layer L{state.layer + 1}</span>
          <Row justify="space-between" align="flex-end">
            <div style={{ display: "flex", flexDirection: "column", gap: 8, width: 120 }}>
              <PToggle k="layerActive" style={{ minHeight: 38 }}>
                Layer on
              </PToggle>
              <PToggle k="reverse" style={{ minHeight: 38 }}>
                Reverse
              </PToggle>
            </div>
            <PKnob k="layerVolume" />
            <PKnob k="layerPan" />
            <PSlider k="velStart" width={170} />
            <PSlider k="velEnd" width={170} />
          </Row>
          <Lcd
            style={{
              height: 38,
              fontSize: 13,
              color: note ? "#ffae3a" : undefined,
              opacity: note ? 1 : 0.7,
            }}
          >
            {note ?? "No overlap with lower layers"}
          </Lcd>
        </>
      )}
      {hasLoop && (
        <>
          <span className="cs-sub">Loop L{state.layer + 1}</span>
          <Row justify="space-between" align="flex-end">
            <div style={{ display: "flex", flexDirection: "column", gap: 8, width: 150 }}>
              <PToggle k="loop" style={{ minHeight: 38 }}>
                Loop
              </PToggle>
              <PChoice k="loopMode" variant="seg" />
            </div>
            <PSlider k="loopStart" width={190} />
            <PSlider k="loopEnd" width={190} />
            <PKnob k="loopCrossfade" size="m" />
          </Row>
        </>
      )}
      {hasPad && (
        <>
          <span className="cs-sub">Pad {padLabel(state.selected)}</span>
          <Row justify="space-between" align="flex-end">
            <PSlider k="padNote" width={170} />
            <PSlider k="outputChannel" width={150} />
            <PSlider k="polyphony" width={130} />
            <PToggle k="mono" style={{ minHeight: 44 }} />
            <PToggle k="mute" style={{ minHeight: 44 }} />
            <PToggle k="solo" style={{ minHeight: 44 }} />
            {host.clearPad && (
              <Key onClick={() => host.clearPad!(state.selected)} style={{ minHeight: 44 }}>
                Clear pad
              </Key>
            )}
          </Row>
        </>
      )}
    </>
  );
}

function ProgramDeck({
  state,
  host,
  initialPage,
}: {
  state: SamplerState;
  host: SamplerHost;
  initialPage?: "sound" | "map";
}) {
  const [page, setPage] = useState<"sound" | "map">(initialPage ?? "sound");
  const hasMap =
    useHas(
      "layerVolume",
      "layerPan",
      "velStart",
      "velEnd",
      "layerActive",
      "reverse",
      "loop",
      "padNote",
      "outputChannel",
      "polyphony",
      "mono",
      "mute",
      "solo",
    ) || !!host.clearPad;
  const pg = hasMap ? page : "sound";
  return (
    <Ctx
      title={`Program · ${padLabel(state.selected)}`}
      chip={system.filter}
      extra={
        <div style={{ display: "flex", gap: 16, alignItems: "center" }}>
          {hasMap && (
            <Seg
              label="Program page"
              value={pg}
              onChange={setPage}
              options={[
                { id: "sound", label: "Sound" },
                { id: "map", label: "Mapping" },
              ]}
            />
          )}
          <div role="tablist" aria-label="Velocity layer" style={{ display: "flex", gap: 8 }}>
            {state.layers.map((l, i) => (
              <Key
                key={i}
                pressed={i === state.layer}
                onClick={() => host.selectLayer(i)}
                label={`Layer ${i + 1}, velocity ${l.velStart}–${l.velEnd}${l.hasSample ? "" : ", empty"}`}
                style={{ opacity: l.hasSample ? 1 : 0.5 }}
              >
                L{i + 1}
              </Key>
            ))}
          </div>
        </div>
      }
    >
      {pg === "map" ? (
        <MappingPage state={state} host={host} />
      ) : (
        <>
          <Row justify="space-between">
            <PKnob k="volume" />
            <PKnob k="pan" />
            <PKnob k="tuneCoarse" />
            <PKnob k="tuneFine" size="m" />
            <Divider />
            <div style={{ display: "flex", flexDirection: "column", gap: 6, width: 176 }}>
              <span className="cs-sub">Trigger</span>
              <PChoice k="triggerMode" variant="seg" />
              <span className="cs-sub">Filter</span>
              <PCycle k="filterShape" />
            </div>
            <PKnob k="filterCutoff" size="xl" />
            <PKnob k="filterResonance" />
            <PKnob k="filterEnvAmount" size="m" />
          </Row>
          <Row justify="space-between" align="flex-end">
            <PKnob k="ampAttackMs" size="m" />
            <PKnob k="ampDecayMs" size="m" />
            <PKnob k="ampSustain" size="m" />
            <PKnob k="ampReleaseMs" size="m" />
            <PSlider k="velocitySensitivity" width={170} />
            <PSlider k="velocityToFilter" width={170} />
            <PSlider k="muteGroup" width={130} />
          </Row>
          <Row justify="space-between" align="flex-end">
            <PKnob k="pitchEnvAmount" size="m" />
            <PKnob k="pitchAttackMs" size="m" />
            <PKnob k="pitchDecayMs" size="m" />
            <Divider />
            <div style={{ display: "flex", flexDirection: "column", gap: 6, width: 130 }}>
              <span className="cs-sub">LFO shape</span>
              <PCycle k="lfoType" />
            </div>
            <PKnob k="lfoRate" size="m" />
            <PKnob k="lfoPitch" size="m" />
            <PKnob k="lfoCutoff" size="m" />
            <PKnob k="lfoVolume" size="m" />
          </Row>
        </>
      )}
    </Ctx>
  );
}

// Steps of the current Time Correct grid; 0 records unquantized (timing kept as nudge).
const QUANTIZE = [
  { id: "0", label: "Off" },
  { id: "1", label: "1 step" },
  { id: "2", label: "2 steps" },
  { id: "4", label: "4 steps" },
];

function SequenceDeck({
  state,
  host,
  pick,
}: {
  state: SamplerState;
  host: SamplerHost;
  pick?: StepPick;
}) {
  // Lengths are whole bars of the current grid (the engine rounds up to
  // bars; 16 steps of 1/8T used to be 5.33 beats and drift).
  const bar = Math.max(1, (state.grid || 4) * 4);
  const lengths = Array.from(
    new Set(
      [1, 2, 4, 8]
        .map((b) => b * bar)
        .filter((n) => n <= 128)
        .concat([state.patternLength]),
    ),
  ).sort((a, b) => a - b);
  const stepTicks = PPQ / Math.max(1, state.grid || 4);
  const key = pick ? `${pick.pad}:${pick.step}` : "";
  const pickOn = !!pick && (state.pattern[pick.pad]?.[pick.step] ?? 0) > 0;
  const nudge = pick ? (state.stepNudges?.[key] ?? 0) : 0;
  const gate = pick ? (state.gateLengths?.[key] ?? stepTicks) : stepTicks;
  const chance = pick ? (state.stepChances?.[key] ?? 100) : 100;
  const ratchet = pick ? (state.stepRatchets?.[key] ?? 1) : 1;
  const chain = state.chain ?? [];
  const reps = state.chainRepeats ?? [];
  const vel = state.stepVelocity ?? 0.85;
  return (
    <Ctx
      title={`Sequence · pattern ${state.patternIndex + 1}`}
      chip={system.env}
      extra={
        <Readout size={13}>
          {state.patternLength} STEPS · {state.bpm.toFixed(1)} BPM
        </Readout>
      }
    >
      <div
        role="group"
        aria-label="Pattern"
        style={{
          display: "grid",
          gridTemplateColumns: `repeat(${Math.min(8, state.patternCount)}, minmax(0, 1fr))`,
          gap: 8,
        }}
      >
        {Array.from({ length: Math.min(8, state.patternCount) }, (_, i) => (
          <Key
            key={i}
            pressed={i === state.patternIndex}
            onClick={() => host.selectPattern(i)}
            style={{ minHeight: 38 }}
          >
            {i + 1}
          </Key>
        ))}
      </div>
      <Row align="flex-end" justify="space-between">
        <Knob
          label="Swing"
          size="xl"
          showValue
          value={state.swing}
          defaultValue={0}
          onChange={host.setSwing}
          spec={{ unit: "%", min: 50, max: 75 }}
        />
        {host.setStepVelocity && (
          <Knob
            label="Step vel"
            size="l"
            showValue
            value={(vel * 127 - 1) / 126}
            defaultValue={(0.85 * 127 - 1) / 126}
            onChange={(v) => host.setStepVelocity!(Math.round(1 + v * 126) / 127)}
            spec={{ min: 1, max: 127, decimals: 0 }}
          />
        )}
        <div
          style={{
            display: "grid",
            gridTemplateColumns: "auto minmax(0, 1fr)",
            columnGap: 12,
            rowGap: 8,
            alignItems: "center",
            flexGrow: 1,
            maxWidth: 470,
          }}
        >
          <span className="cs-sub">Time correct</span>
          <Seg
            label="Time correct"
            value={String(state.grid)}
            onChange={(g) => host.setGrid(Number(g))}
            options={GRIDS.map((g) => ({ id: String(g.n), label: g.l }))}
          />
          <span className="cs-sub">Length</span>
          <Seg
            label="Pattern length"
            value={String(state.patternLength)}
            onChange={(l) => host.setPatternLength(Number(l))}
            options={lengths.map((l) => ({
              id: String(l),
              label: l % bar === 0 ? `${l / bar} bar${l / bar > 1 ? "s" : ""}` : `${l}`,
            }))}
          />
          {host.setSwingDivision && (
            <>
              <span className="cs-sub">Swing grid</span>
              <Seg
                label="Swing grid"
                value={String(state.swingDivision ?? 16)}
                onChange={(d) => host.setSwingDivision!(Number(d))}
                options={[
                  { id: "8", label: "1/8" },
                  { id: "16", label: "1/16" },
                ]}
              />
            </>
          )}
          {host.setBeatsPerBar && (
            <>
              <span className="cs-sub">Time sig</span>
              <Seg
                label="Time signature"
                value={String(state.beatsPerBar ?? 4)}
                onChange={(b) => host.setBeatsPerBar!(Number(b))}
                options={[
                  { id: "3", label: "3/4" },
                  { id: "4", label: "4/4" },
                  { id: "5", label: "5/4" },
                  { id: "7", label: "7/4" },
                ]}
              />
            </>
          )}
          {host.setRecordQuantize && (
            <>
              <span className="cs-sub">Rec quantize</span>
              <Seg
                label="Record quantize"
                value={String(state.recordQuantize ?? 1)}
                onChange={(q) => host.setRecordQuantize!(Number(q))}
                options={QUANTIZE}
              />
            </>
          )}
        </div>
      </Row>
      {(host.setStepNudge ||
        host.setStepLength ||
        host.setStepProbability ||
        host.setStepRatchet) && (
        <Row align="flex-end" justify="space-between">
          <Lcd style={{ height: 48, fontSize: 13, minWidth: 190 }}>
            {pickOn && pick
              ? `STEP ${padLabel(pick.pad)} · ${pick.step + 1}`
              : "RIGHT-CLICK A STEP TO EDIT"}
          </Lcd>
          {host.setStepNudge && (
            <Slider
              label="Shift timing"
              width={260}
              value={pickOn ? nudge / stepTicks + 0.5 : 0.5}
              defaultValue={0.5}
              onChange={(v) =>
                pick &&
                pickOn &&
                host.setStepNudge!(pick.pad, pick.step, Math.round((v - 0.5) * stepTicks))
              }
              spec={{ unit: "ticks", min: -stepTicks / 2, max: stepTicks / 2, decimals: 0 }}
            />
          )}
          {host.setStepLength && (
            <Knob
              label="Gate length"
              size="l"
              showValue
              value={pickOn ? Math.log2(gate / stepTicks + 0.0001) / 8 + 0.25 : 0.25}
              defaultValue={0.25}
              onChange={(v) =>
                pick &&
                pickOn &&
                host.setStepLength!(
                  pick.pad,
                  pick.step,
                  Math.max(1, Math.round(Math.pow(2, (v - 0.25) * 8) * stepTicks)),
                )
              }
              spec={{ unit: "steps", min: 0.25, max: 64, curve: "log", decimals: 2 }}
            />
          )}
          {host.setStepProbability && (
            <Knob
              label="Chance"
              size="l"
              showValue
              value={pickOn ? chance / 100 : 1}
              defaultValue={1}
              onChange={(v) =>
                pick && pickOn && host.setStepProbability!(pick.pad, pick.step, Math.round(v * 100))
              }
              spec={{ unit: "%", min: 0, max: 100, decimals: 0 }}
            />
          )}
          {host.setStepRatchet && (
            <Seg
              label="Ratchet"
              value={String(ratchet)}
              onChange={(r) =>
                pick && pickOn && host.setStepRatchet!(pick.pad, pick.step, Number(r))
              }
              options={[1, 2, 3, 4, 6, 8].map((n) => ({
                id: String(n),
                label: n === 1 ? "Off" : `${n}x`,
              }))}
            />
          )}
        </Row>
      )}
      <Row justify="space-between" align="center">
        <Key
          pressed={state.chainMode}
          onClick={() => host.setChainMode(!state.chainMode)}
          style={{ minHeight: 48 }}
        >
          Chain mode
        </Key>
        {host.setChain && (
          <>
            <Key
              onClick={() => host.setChain!([...chain, state.patternIndex])}
              disabled={chain.length >= 64}
              style={{ minHeight: 48 }}
            >
              + Pattern {state.patternIndex + 1}
            </Key>
            <Key
              onClick={() => host.setChain!([])}
              disabled={!chain.length}
              style={{ minHeight: 48 }}
            >
              Clear chain
            </Key>
            {host.setChainRepeats && chain.length > 0 && (
              <Key
                style={{ minHeight: 48 }}
                onClick={() => {
                  const next = chain.map((_, i) => reps[i] ?? 1);
                  const last = next.length - 1;
                  const cur = next[last];
                  next[last] = cur === 1 ? 2 : cur === 2 ? 4 : cur === 4 ? 8 : 1;
                  host.setChainRepeats!(next);
                }}
              >{`Last ×${reps[chain.length - 1] ?? 1}`}</Key>
            )}
            <Lcd
              style={{
                height: 48,
                fontSize: 14,
                flexGrow: 1,
                minWidth: 0,
                overflow: "hidden",
                whiteSpace: "nowrap",
              }}
            >
              {chain.length
                ? chain
                    .map((p, i) => `${p + 1}${(reps[i] ?? 1) > 1 ? `×${reps[i]}` : ""}`)
                    .join(" › ")
                : "Chain empty"}
            </Lcd>
          </>
        )}
        {host.setRowMute && state.pads[state.selected] && (
          <Key
            pressed={state.rowMutes?.includes(state.selected) ?? false}
            onClick={() =>
              host.setRowMute!(state.selected, !(state.rowMutes?.includes(state.selected) ?? false))
            }
            style={{ minHeight: 48 }}
          >
            Mute {padLabel(state.selected)}
          </Key>
        )}
        <Key onClick={() => host.clearPattern()} style={{ minHeight: 48 }}>
          Clear pattern
        </Key>
      </Row>
    </Ctx>
  );
}

function BrowseDeck({ state, host }: { state: SamplerState; host: SamplerHost }) {
  const groups: FileActionGroup[] = [
    {
      title: `Pad ${padLabel(state.selected)} · L${state.layer + 1}`,
      actions: [
        { label: "Load file…", run: host.loadSample },
        ...(host.clearPad
          ? [{ label: "Clear pad", run: () => host.clearPad!(state.selected) }]
          : []),
      ],
    },
    ...(host.fileActions ?? []),
  ];
  return (
    <Ctx
      title="Browse · Files"
      chip={system.source}
      extra={host.dropFiles ? <Readout size={13}>DROP AUDIO ON A PAD</Readout> : undefined}
    >
      <div style={{ display: "grid", gridTemplateColumns: "repeat(2, minmax(0, 1fr))", gap: 16 }}>
        {groups.map((g) => (
          <div
            key={g.title}
            role="group"
            aria-label={g.title}
            style={{ display: "flex", flexDirection: "column", gap: 8 }}
          >
            <span className="cs-sub">{g.title}</span>
            <div
              style={{
                display: "grid",
                gridTemplateColumns: `repeat(${g.actions.length}, minmax(0, 1fr))`,
                gap: 8,
              }}
            >
              {g.actions.map((a) => (
                <Key key={a.label} onClick={a.run} style={{ minHeight: 44 }}>
                  {a.label}
                </Key>
              ))}
            </div>
          </div>
        ))}
      </div>
    </Ctx>
  );
}

function PerformDeck({ perform }: { perform: PerformHost }) {
  const p = perform.settings;
  const n = perform.progressions.length;
  const scaleOpts = ["Song", ...perform.scales.map((x) => x.name)];
  const scaleIdx = p.scale ? perform.scales.findIndex((x) => x.id === p.scale) + 1 : 0;
  const baseOpts = ["Key", ...NOTE_NAMES];
  const chordIdx = Math.max(0, perform.chromaticChords.indexOf(p.chromaticChord));
  const step = (d: number) =>
    n && perform.selectProgression((perform.progressionIndex + d + n) % n);
  return (
    <Ctx
      title="Pad Perform"
      chip={system.mod}
      extra={<Readout size={13}>PADS PLAY {PERFORM_MODES[p.mode]?.toUpperCase()}</Readout>}
    >
      <Row justify="flex-start" align="center" gap={16}>
        <Key
          pressed={p.on}
          onClick={() => perform.set({ on: !p.on })}
          style={{ minHeight: 48, minWidth: 150, fontSize: 14 }}
        >
          Perform {p.on ? "on" : "off"}
        </Key>
        <div
          role="group"
          aria-label="Perform mode"
          style={{
            display: "grid",
            gridTemplateColumns: "repeat(5, minmax(0, 1fr))",
            gap: 8,
            flexGrow: 1,
          }}
        >
          {PERFORM_MODES.map((l, i) => (
            <Key
              key={l}
              pressed={p.mode === i}
              onClick={() => perform.set({ mode: i })}
              style={{ minHeight: 48 }}
            >
              {l}
            </Key>
          ))}
        </div>
      </Row>
      {p.mode === 4 ? (
        <Row justify="flex-start" align="center" gap={8}>
          <Key label="Previous progression" onClick={() => step(-1)} disabled={n < 2}>
            ◂
          </Key>
          <Lcd
            style={{
              height: 44,
              fontSize: 14,
              width: 260,
              overflow: "hidden",
              whiteSpace: "nowrap",
            }}
          >
            {perform.progressions[perform.progressionIndex]?.name ?? "No progressions"}
          </Lcd>
          <Key label="Next progression" onClick={() => step(1)} disabled={n < 2}>
            ▸
          </Key>
          <div style={{ width: 8 }} />
          {perform.importProgression && (
            <Key onClick={perform.importProgression} style={{ minHeight: 44 }}>
              Import
            </Key>
          )}
          {perform.progressionFromMidi && (
            <Key onClick={perform.progressionFromMidi} style={{ minHeight: 44 }}>
              From MIDI
            </Key>
          )}
          {perform.exportProgression && (
            <Key onClick={perform.exportProgression} disabled={!n} style={{ minHeight: 44 }}>
              Export
            </Key>
          )}
        </Row>
      ) : (
        <Row justify="space-between" align="flex-end">
          <div style={{ display: "flex", flexDirection: "column", gap: 8 }}>
            <span className="cs-sub">Octave</span>
            <Seg
              label="Octave"
              value={String(p.octave)}
              onChange={(o) => perform.set({ octave: Number(o) })}
              options={[-2, -1, 0, 1, 2].map((o) => ({
                id: String(o),
                label: o > 0 ? `+${o}` : String(o),
              }))}
            />
          </div>
          <Knob
            label="Scale"
            size="l"
            showValue
            value={scaleOpts.length > 1 ? scaleIdx / (scaleOpts.length - 1) : 0}
            defaultValue={0}
            onChange={(v) => {
              const i = Math.round(v * (scaleOpts.length - 1));
              perform.set({ scale: i === 0 ? "" : perform.scales[i - 1].id });
            }}
            spec={{ options: scaleOpts }}
          />
          <Knob
            label="Base"
            size="l"
            showValue
            value={(p.basePc + 1) / 12}
            defaultValue={0}
            onChange={(v) => perform.set({ basePc: Math.round(v * 12) - 1 })}
            spec={{ options: baseOpts }}
          />
          <Key
            pressed={p.constrain}
            onClick={() => perform.set({ constrain: !p.constrain })}
            style={{ minHeight: 44 }}
          >
            In scale
          </Key>
          {p.mode === 2 && (
            <div style={{ display: "flex", flexDirection: "column", gap: 8 }}>
              <span className="cs-sub">Chord size</span>
              <Seg
                label="Chord size"
                value={String(p.chordType)}
                onChange={(t) => perform.set({ chordType: Number(t) })}
                options={["Triads", "7ths", "9ths"].map((l, i) => ({ id: String(i), label: l }))}
              />
            </div>
          )}
          {p.mode === 3 && perform.chromaticChords.length > 1 && (
            <Knob
              label="Quality"
              size="l"
              showValue
              value={chordIdx / (perform.chromaticChords.length - 1)}
              defaultValue={0}
              onChange={(v) =>
                perform.set({
                  chromaticChord:
                    perform.chromaticChords[Math.round(v * (perform.chromaticChords.length - 1))],
                })
              }
              spec={{ options: perform.chromaticChords }}
            />
          )}
        </Row>
      )}
      <Row justify="flex-start" align="flex-end" gap={16}>
        <div style={{ display: "flex", flexDirection: "column", gap: 8 }}>
          <span className="cs-sub">Banks</span>
          <Seg
            label="Bank behaviour"
            value={String(p.bankBehaviour)}
            onChange={(b) => perform.set({ bankBehaviour: Number(b) })}
            options={[
              { id: "0", label: "Continue" },
              { id: "1", label: "Restart" },
            ]}
          />
        </div>
        <Knob
          label="Velocity"
          size="m"
          showValue
          value={(p.velocity - 1) / 126}
          defaultValue={(100 - 1) / 126}
          onChange={(v) => perform.set({ velocity: Math.round(1 + v * 126) })}
          spec={{ min: 1, max: 127, decimals: 0 }}
        />
        <Key onClick={perform.save} style={{ minHeight: 44 }}>
          Save as default
        </Key>
      </Row>
    </Ctx>
  );
}

function RecordDeck({ host }: { host: SamplerHost }) {
  const inp = host.input!;
  return (
    <Ctx title="Record" chip={system.dynamics}>
      <Row justify="flex-start" align="center" gap={16}>
        <Seg
          label="Input source"
          value={String(inp.source)}
          onChange={(s) => inp.setSource(Number(s))}
          options={inp.sources.map((s, i) => ({ id: String(i), label: s }))}
          style={{ flexGrow: 1 }}
        />
        {inp.enabled === false && inp.enable && (
          <Key onClick={inp.enable} style={{ minHeight: 44 }}>
            Enable audio input
          </Key>
        )}
      </Row>
      <Row align="flex-end">
        <Knob
          label="Threshold"
          size="xl"
          showValue
          value={inp.threshold}
          defaultValue={0.5}
          onChange={inp.setThreshold}
          spec={{ unit: "dB", min: -60, max: 0 }}
        />
        <Key
          pressed={inp.monitor}
          disabled={inp.sources[inp.source] === "Resample"}
          onClick={() => inp.setMonitor(!inp.monitor)}
          style={{ minHeight: 48, minWidth: 130 }}
        >
          Monitor
        </Key>
        <Key
          pressed={inp.armed}
          onClick={() => inp.arm(!inp.armed)}
          style={{ minHeight: 64, minWidth: 180, fontSize: 16, color: "#ff5a4a" }}
        >
          ● {inp.armed ? "Armed" : "Arm"}
        </Key>
        <Key onClick={() => inp.stop()} style={{ minHeight: 64, minWidth: 120 }}>
          ■ Stop
        </Key>
      </Row>
      <Lcd style={{ height: 38, fontSize: 13 }}>
        Records into {inp.target ?? "the selected pad"} · saved as a WAV · then Trim → Chop →
        Program
      </Lcd>
    </Ctx>
  );
}

/* ------------------------------------------------------------------ panel */

export function SamplerPanel({
  state,
  host,
  program,
  playhead,
  initialMode,
  initialProgramPage,
}: SamplerPanelProps) {
  const [mode, setMode] = useState<Mode>(initialMode ?? (state.slices.length ? "chop" : "trim"));
  const [fullLevel, setFullLevel] = useState(false);
  const [sixteen, setSixteen] = useState(false);
  const [repeat, setRepeat] = useState(false);
  // Step picked for editing (right-click / shift-click in the grid).
  const [pick, setPick] = useState<StepPick>(null);
  const modes = MODES.filter((m) =>
    m.id === "record"
      ? !!host.input
      : m.id === "browse"
        ? !!(host.library || host.fileActions?.length)
        : m.id === "perform"
          ? !!host.perform
          : true,
  );
  const s = state.sample;

  const screen = (
    <>
      {mode === "sequence" ? (
        <SequenceGrid state={state} host={host} pick={pick} onPick={setPick} />
      ) : mode === "record" && host.input ? (
        <RecordScreen host={host} />
      ) : mode === "perform" && host.perform ? (
        <PerformScreen perform={host.perform} />
      ) : mode === "browse" && host.library ? (
        <LibraryScreen
          lib={host.library}
          target={`${padLabel(state.selected)} · L${state.layer + 1}`}
        />
      ) : (
        <SampleScreen state={state} mode={mode} playhead={playhead} host={host} />
      )}
      <Overlay interactive style={{ left: 16, top: 12, display: "flex", gap: 8 }}>
        <div role="tablist" aria-label="Sampler workflow" style={{ display: "flex", gap: 8 }}>
          {modes.map((m) => (
            <button
              key={m.id}
              type="button"
              role="tab"
              aria-selected={m.id === mode}
              className={`cs-key${m.id === mode ? " cs-on" : ""}`}
              onClick={() => setMode(m.id)}
              style={{ minHeight: 36, display: "flex", alignItems: "center", gap: 8 }}
            >
              <span
                className="cs-readout"
                style={{
                  fontSize: 11,
                  textShadow: "none",
                  color: m.id === mode ? "#ffae3a" : "#6f6c66",
                }}
              >
                {m.n}
              </span>
              {m.label}
            </button>
          ))}
        </div>
      </Overlay>
      {(mode === "trim" ||
        mode === "chop" ||
        mode === "program" ||
        (mode === "browse" && !host.library)) && (
        <Overlay
          interactive
          style={{ right: 16, top: 12, display: "flex", gap: 8, alignItems: "center" }}
        >
          <Lcd style={{ height: 36, fontSize: 13, gap: 10 }}>
            {s ? (
              <>
                {s.name}
                {s.seconds !== undefined && (
                  <span style={{ opacity: 0.6 }}>
                    {fmtSec(s.seconds)} s
                    {s.sampleRate ? ` · ${(s.sampleRate / 1000).toFixed(1)}k` : ""}
                  </span>
                )}
              </>
            ) : (
              "EMPTY PAD"
            )}
          </Lcd>
          <Key onClick={host.loadSample} style={{ minHeight: 36 }}>
            Load
          </Key>
        </Overlay>
      )}
    </>
  );

  // Digits 1-5 pick the workflow mode while focus is inside the sampler; the
  // event is marked handled so an app-level "digits switch tabs" shortcut
  // (which should check defaultPrevented) leaves it alone.
  const onKeyDown = (e: ReactKeyboardEvent) => {
    if (e.metaKey || e.ctrlKey || e.altKey) return;
    const t = e.target as HTMLElement;
    if (t.tagName === "INPUT" || t.tagName === "TEXTAREA" || t.tagName === "SELECT") return;
    if (t.isContentEditable) {
      e.stopPropagation();
      return;
    }
    const m = modes.find((x) => x.n === e.key);
    if (m) {
      e.preventDefault();
      setMode(m.id);
    }
  };

  return (
    <div onKeyDown={onKeyDown} style={{ display: "contents" }}>
      <PanelScope
        schema={samplerProgramSchema}
        specs={program.specs}
        store={program.store}
        map={program.map}
      >
        <PluginFrame
          name="SAMPLER"
          tag="CHORDSTUDIO · PADS"
          subtitle="Record → trim → chop → program → sequence"
          presetName={state.kitName}
          screenHeight={320}
          headerRight={
            <>
              <Key
                label="Play"
                pressed={state.playing}
                onClick={() => host.transport(state.playing ? "stop" : "play")}
                style={{ minHeight: 40, minWidth: 48 }}
              >
                ▶
              </Key>
              <Key
                label="Stop"
                onClick={() => host.transport("stop")}
                style={{ minHeight: 40, minWidth: 48 }}
              >
                ■
              </Key>
              <Key
                label="Record pattern"
                pressed={state.recording}
                onClick={() => host.transport("record")}
                style={{ minHeight: 40, minWidth: 48, color: "#ff5a4a" }}
              >
                ●
              </Key>
              <Lcd style={{ height: 40, fontSize: 16, gap: 8 }}>
                <span style={{ opacity: 0.6 }}>BPM</span>
                {state.bpm.toFixed(1)}
              </Lcd>
            </>
          }
          screen={screen}
        >
          <Group
            title="Pads"
            chip={system.filter}
            style={{ width: 500, boxSizing: "border-box" }}
            extra={
              <Seg
                label="Pad bank"
                value={String(state.bank)}
                onChange={(i) => host.selectBank(Number(i))}
                options={BANKS.map((b, i) => ({ id: String(i), label: b }))}
              />
            }
          >
            <Row gap={10} align="flex-start">
              <Pads
                pads={state.pads}
                bank={state.bank}
                selected={state.selected}
                onSelect={host.selectPad}
                onTrigger={host.trigger}
                onRelease={host.release}
                onDropFiles={host.dropFiles}
                fullLevel={fullLevel}
                sixteenLevels={sixteen}
                size={80}
              />
              <div style={{ display: "flex", flexDirection: "column", gap: 8, flexGrow: 1 }}>
                <Key
                  pressed={fullLevel}
                  onClick={() => setFullLevel(!fullLevel)}
                  style={{ minHeight: 44 }}
                >
                  Full level
                </Key>
                <Key
                  pressed={sixteen}
                  onClick={() => setSixteen(!sixteen)}
                  style={{ minHeight: 44 }}
                >
                  16 levels
                </Key>
                {host.noteRepeat && (
                  <Key
                    pressed={repeat}
                    onClick={() => {
                      setRepeat(!repeat);
                      host.noteRepeat!(!repeat, state.grid);
                    }}
                    style={{ minHeight: 44 }}
                  >
                    Note repeat
                  </Key>
                )}
                <Key
                  onClick={() => host.transport("record")}
                  pressed={state.recording}
                  style={{ minHeight: 44, color: "#ff5a4a" }}
                >
                  Rec pads
                </Key>
              </div>
            </Row>
          </Group>
          {mode === "chop" && <ChopDeck state={state} host={host} />}
          {mode === "trim" && <TrimDeck state={state} host={host} />}
          {mode === "program" && (
            <ProgramDeck state={state} host={host} initialPage={initialProgramPage} />
          )}
          {mode === "sequence" && <SequenceDeck state={state} host={host} pick={pick} />}
          {mode === "record" && host.input && <RecordDeck host={host} />}
          {mode === "browse" && <BrowseDeck state={state} host={host} />}
          {mode === "perform" && host.perform && <PerformDeck perform={host.perform} />}
        </PluginFrame>
      </PanelScope>
    </div>
  );
}
