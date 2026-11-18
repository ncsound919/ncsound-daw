import { addBackendEventListener, getNativeFunction, isEmbedded } from "./juce";
import type { NativeRoute } from "./nativeRoutes.generated";
import type { AppState, AutomationLaneDraft, MidiNote, PitchState, TransportState } from "../types";

export interface NativeAudioType {
  name: string;
  outputs: string[];
}

export interface NativeAudioState {
  deviceType: string;
  outputDevice: string;
  sampleRate: number;
  bufferSize: number;
  outputChannels: number;
  inputChannels: number;
  open: boolean;
  currentRate: number;
  currentBuffer: number;
  device: string;
  overruns: number;
  blocks: number;
  types: NativeAudioType[];
}

export interface AudioApplyResult {
  ok: boolean;
  error: string;
  note: string;
  state: NativeAudioState;
}

function call<T>(name: NativeRoute, ...args: unknown[]): Promise<T> {
  return getNativeFunction(name)(...args) as Promise<T>;
}

export const backend = {
  embedded: isEmbedded(),

  getState(): Promise<AppState> {
    return call<AppState>("getState");
  },

  getTransport(): Promise<TransportState> {
    return call<TransportState>("getTransport");
  },

  getPitch(): Promise<{
    globalPitch: PitchState;
    tracks: { index: number; pitch: PitchState; instrumentPitch: PitchState }[];
  }> {
    return call("getPitch");
  },

  setGlobalPitch(patch: Partial<PitchState>): Promise<AppState> {
    return call<AppState>("setGlobalPitch", patch);
  },

  setTrackPitch(track: number, patch: Partial<PitchState>): Promise<AppState> {
    return call<AppState>("setTrackPitch", track, patch);
  },

  setInstrumentPitch(track: number, patch: Partial<PitchState>): Promise<AppState> {
    return call<AppState>("setInstrumentPitch", track, patch);
  },

  setPlaying(playing: boolean): Promise<AppState> {
    return call<AppState>("setPlaying", playing);
  },

  /** 1-based bar. The host converts bars to seconds against the live tempo. */
  setPositionBar(bar: number): Promise<AppState> {
    return call<AppState>("setPositionBar", bar);
  },

  setBpm(bpm: number): Promise<AppState> {
    return call<AppState>("setBpm", bpm);
  },

  setLoopEnabled(on: boolean): Promise<AppState> {
    return call<AppState>("setLoopEnabled", on);
  },

  setMetronome(on: boolean): Promise<AppState> {
    return call<AppState>("setMetronome", on);
  },

  setTrackGain(track: number, db: number): Promise<AppState> {
    return call<AppState>("setTrackGain", track, db);
  },

  setTrackPan(track: number, pan: number): Promise<AppState> {
    return call<AppState>("setTrackPan", track, pan);
  },

  setTrackMuted(track: number, muted: boolean): Promise<AppState> {
    return call<AppState>("setTrackMuted", track, muted);
  },

  setTrackSolo(track: number, soloed: boolean): Promise<AppState> {
    return call<AppState>("setTrackSolo", track, soloed);
  },

  setMasterGain(db: number): Promise<AppState> {
    return call<AppState>("setMasterGain", db);
  },

  setTrackSendA(track: number, amount: number): Promise<AppState> {
    return call<AppState>("setTrackSendA", track, amount);
  },

  setTrackSendB(track: number, amount: number): Promise<AppState> {
    return call<AppState>("setTrackSendB", track, amount);
  },

  setRecordArmed(track: number, armed: boolean): Promise<AppState> {
    return call<AppState>("setRecordArmed", track, armed);
  },

  startRecording(): Promise<AppState> {
    return call<AppState>("startRecording");
  },

  stopRecording(): Promise<AppState> {
    return call<AppState>("stopRecording");
  },

  midiLearn(key: string): Promise<AppState> {
    return call<AppState>("midiLearn", key);
  },

  setMasterDim(on: boolean): Promise<AppState> {
    return call<AppState>("setMasterDim", on);
  },

  setMasterMono(on: boolean): Promise<AppState> {
    return call<AppState>("setMasterMono", on);
  },

  setSelectedTrack(track: number): Promise<AppState> {
    return call<AppState>("setSelectedTrack", track);
  },

  /** Arrangement index: the position in `getState().clips`, audio or MIDI. */
  editTimelineClip(index: number, patch: Record<string, unknown>): Promise<AppState> {
    return call<AppState>("setClipEdit", index, patch);
  },

  splitTimelineClip(index: number, bar: number): Promise<AppState> {
    return call<AppState>("setClipSplit", index, bar);
  },

  duplicateTimelineClip(index: number): Promise<AppState> {
    return call<AppState>("setClipDuplicate", index);
  },

  deleteTimelineClip(index: number): Promise<AppState> {
    return call<AppState>("setClipDelete", index);
  },

  /** Voice-to-MIDI: turns the selected audio clip into a MIDI clip on the
      same track. The host answers with the full snapshot including it. */
  transcribeTimelineClip(index: number): Promise<AppState> {
    return call<AppState>("transcribeClip", index);
  },

  /** Replaces a MIDI clip's notes; the engine sanitises them on arrival. */
  setClipNotes(clip: number, notes: MidiNote[]): Promise<AppState> {
    return call<AppState>("setClipNotes", clip, notes);
  },

  /** Writes one volume/pan lane. Values are 0..1; pan is converted natively. */
  setAutomationLane(track: number, lane: AutomationLaneDraft): Promise<AppState> {
    return call<AppState>("setAutomationLane", track, lane);
  },

  /** Loads a catalogue row by its stable registry id into a user insert slot
      (effects) or the track's instrument lane (instruments). */
  loadPlugin(track: number, slot: number, id: string): Promise<AppState> {
    return call<AppState>("loadPlugin", track, slot, id);
  },

  loadMasterPlugin(id: string): Promise<AppState> {
    return call<AppState>("loadMasterPlugin", id);
  },

  /** Bypasses one insert slot without unloading it. */
  setInsertBypassed(track: number, slot: number, bypassed: boolean): Promise<AppState> {
    return call<AppState>("setInsertBypassed", track, slot, bypassed);
  },

  /** Moves an insert onto an *empty* slot; occupied targets are refused. */
  moveInsert(track: number, from: number, to: number): Promise<AppState> {
    return call<AppState>("moveInsert", track, from, to);
  },

  /** Opens the hosted plug-in editor for an insert, instrument (-1) or master (-2) slot. */
  openPluginEditor(track: number, slot: number): Promise<{ ok: boolean; error?: string }> {
    return call<{ ok: boolean; error?: string }>("openPluginEditor", track, slot);
  },

  /** Turns a track's automation read on or off; the lanes are kept. */
  setAutomationRead(track: number, enabled: boolean): Promise<AppState> {
    return call<AppState>("setAutomationRead", track, enabled);
  },

  /** Captures the master insert as the natively held A/B snapshot. */
  captureMasterAb(): Promise<AppState> {
    return call<AppState>("captureMasterAb");
  },

  /** Recalls the captured master snapshot; refuses when empty or missing. */
  recallMasterAb(): Promise<AppState> {
    return call<AppState>("recallMasterAb");
  },

  /** Writes one baked MixRack parameter by its Overlay id (e.g. `lowGain`). */
  setChannelParam(track: number, param: string, value: number): Promise<AppState> {
    return call<AppState>("setChannelParam", track, param, value);
  },

  /** Writes one baked Overlay Master parameter by its Overlay id. */
  setMasterParam(param: string, value: number): Promise<AppState> {
    return call<AppState>("setMasterParam", param, value);
  },

  /** Starts the folder scan; the host pushes state when it finishes. */
  scanPlugins(): Promise<AppState> {
    return call<AppState>("scanPlugins");
  },

  mixologyPresets(): Promise<string[]> {
    return call<string[]>("mixologyPresets");
  },

  applyMixologyPreset(index: number): Promise<{ state: AppState; report: string }> {
    return call("applyMixologyPreset", index);
  },

  /**
   * Render (or take) a mix, measure it with Earshot, and apply a bounded
   * Overlay Channel Strip / Master first pass. An empty audioPath renders the
   * current session. `referencePath` is optional.
   */
  mixologyAutopilot(
    brief: string,
    audioPath: string,
    referencePath: string,
    depth: number,
    targetLufs: number,
  ): Promise<{ state: AppState; report: string; summary: string; assessment: string }> {
    return call("mixologyAutopilot", brief, audioPath, referencePath, depth, targetLufs);
  },

  importAudio(): Promise<AppState> {
    return call<AppState>("importAudio");
  },

  openSession(): Promise<AppState> {
    return call<AppState>("openSession");
  },

  saveSession(): Promise<AppState> {
    return call<AppState>("saveSession");
  },

  saveSessionAs(): Promise<AppState> {
    return call<AppState>("saveSessionAs");
  },

  mixdownWav(): Promise<AppState> {
    return call<AppState>("mixdownWav");
  },

  exportStems(): Promise<AppState> {
    return call<AppState>("exportStems");
  },

  collectMedia(): Promise<AppState> {
    return call<AppState>("collectMedia");
  },

  removeMasterPlugin(): Promise<AppState> {
    return call<AppState>("removeMasterPlugin");
  },

  undo(): Promise<AppState> {
    return call<AppState>("undo");
  },

  redo(): Promise<AppState> {
    return call<AppState>("redo");
  },

  addMarker(): Promise<AppState> {
    return call<AppState>("addMarker");
  },

  nextMarker(): Promise<AppState> {
    return call<AppState>("nextMarker");
  },

  previousMarker(): Promise<AppState> {
    return call<AppState>("previousMarker");
  },

  deleteMarker(): Promise<AppState> {
    return call<AppState>("deleteMarker");
  },

  reverseClip(): Promise<AppState> {
    return call<AppState>("reverseClip");
  },

  normalizeClip(): Promise<AppState> {
    return call<AppState>("normalizeClip");
  },

  setLoopBars(startBar: number, endBar: number): Promise<AppState> {
    return call<AppState>("setLoopBars", startBar, endBar);
  },

  setActiveTrackCount(count: number): Promise<AppState> {
    return call<AppState>("setActiveTrackCount", count);
  },

  getAudioState(): Promise<NativeAudioState> {
    return call<NativeAudioState>("getAudioState");
  },

  applyAudioSettings(
    deviceType: string,
    outputDevice: string,
    sampleRate: number,
    bufferSize: number,
  ): Promise<AudioApplyResult> {
    return call<AudioApplyResult>(
      "applyAudioSettings",
      deviceType,
      outputDevice,
      sampleRate,
      bufferSize,
    );
  },

  onStateChanged(cb: (state: AppState) => void): () => void {
    return addBackendEventListener("stateChanged", (data) => {
      if (data && typeof data === "object") cb(data as AppState);
    });
  },

  onTransportChanged(cb: (transport: TransportState) => void): () => void {
    return addBackendEventListener("transportChanged", (data) => {
      if (data && typeof data === "object") cb(data as TransportState);
    });
  },
};
