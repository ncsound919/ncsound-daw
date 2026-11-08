import { addBackendEventListener, getNativeFunction, isEmbedded } from "./juce";
import type { NativeRoute } from "./nativeRoutes.generated";
import type { AppState, AutomationLaneDraft, MidiNote, PitchState, TransportState } from "../types";

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

  getPitch(): Promise<{ globalPitch: PitchState; tracks: { index: number; pitch: PitchState; instrumentPitch: PitchState }[] }> {
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

  /** Writes one baked MixRack parameter by its Overlay id (e.g. `lowGain`). */
  setChannelParam(track: number, param: string, value: number): Promise<AppState> {
    return call<AppState>("setChannelParam", track, param, value);
  },

  /** Starts the folder scan; the host pushes state when it finishes. */
  scanPlugins(): Promise<AppState> {
    return call<AppState>("scanPlugins");
  },

  undo(): Promise<AppState> {
    return call<AppState>("undo");
  },

  redo(): Promise<AppState> {
    return call<AppState>("redo");
  },

  setLoopBars(startBar: number, endBar: number): Promise<AppState> {
    return call<AppState>("setLoopBars", startBar, endBar);
  },

  setActiveTrackCount(count: number): Promise<AppState> {
    return call<AppState>("setActiveTrackCount", count);
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
