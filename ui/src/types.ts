export interface PitchState {
  transpose: number; // semitones -48..48
  pitchCents: number; // cents -100..100
  tuningHz: number; // A4 415..466, default 440
}

export interface InsertSlot {
  name: string;
  /** The saved plug-in is no longer installed. */
  missing: boolean;
  /** Audio passes through untouched; the plug-in stays loaded. */
  bypassed?: boolean;
}

export interface TrackInfo {
  index: number;
  name: string;
  instrumentName: string;
  gainDb: number;
  pan: number;
  muted: boolean;
  soloed: boolean;
  transpose?: number;
  pitchCents?: number;
  tuningHz?: number;
  instrumentPitch?: PitchState;
  /** Send A/B amounts, 0..1, as the console stores them. */
  sendA?: number;
  sendB?: number;
  /** Stereo width, percent (100 = unchanged). */
  width?: number;
  armed?: boolean;
  inserts?: InsertSlot[];
}

/** Mirrors nc::engine::MidiNote: clip-relative beats, MIDI pitch and velocity. */
export interface MidiNote {
  startBeat: number;
  lengthBeats: number;
  pitch: number; // 0..127, 60 == middle C
  velocity: number; // 1..127
  channel?: number;
}

/** High-frequency transport projection, pushed by the host while the
    workspace is visible. Kept apart from AppState so model updates do not
    arrive 30 times a second. */
export interface TransportState {
  playing: boolean;
  positionSeconds: number;
  /** 1-based musical position, fractional within the bar. */
  positionBar: number;
  bpm: number;
  loopEnabled: boolean;
  metronome: boolean;
  masterPeakLeft: number;
  masterPeakRight: number;
  masterDim: boolean;
  masterMono: boolean;
  /** True while armed tracks are capturing audio. */
  recording?: boolean;
  loopStartBar?: number;
  loopEndBar?: number;
  trackPeaks: number[];
}

/** One row of the arrangement, in the order the host publishes it. Bars are
    1-based, matching the host. Audio and MIDI clips share this shape; `kind`
    says which half of `peaks`/`notes` is populated. */
export interface TimelineClip {
  /** Host-assigned position; equal to the array index today. */
  index?: number;
  track: number;
  startBar: number;
  lengthBars: number;
  /** Audio asset id. Absent on MIDI clips, which carry notes instead. */
  assetId?: number;
  gainDb: number;
  name: string;
  /** The host always sets it; older fixtures without it are audio. */
  kind?: "audio" | "midi";
  /** Peak envelope of the audio this clip plays, 0..1. Empty when the host
      could not read the asset (a streamed import only keeps a window resident). */
  peaks?: number[];
  fadeInBars?: number;
  fadeOutBars?: number;
  /** Equal-power overlap in bars; 0 means the linear fades apply. */
  crossfadeBars?: number;
  /** Active comp take, 0-based. */
  compTake?: number;
  /** Clip-relative MIDI notes. Present for MIDI clips. */
  notes?: MidiNote[];
}

/** One entry of the plug-in catalogue the browser lists. `id` is the stable
    registry key the host loads by; `name` alone is not unique. */
export interface PluginInfo {
  id: string;
  name: string;
  vendor?: string;
  category?: "instrument" | "effect";
  format?: string;
  path?: string;
  /** The row exists in the registry but no installed bundle is linked to it. */
  missing?: boolean;
  /** True when a folder scan produced an instantiable description. */
  scanned?: boolean;
  /** True for plugins/registry.json rows, which own the display identity. */
  registryRow?: boolean;
}

/** The baked Overlay channel strip behind a track, 1:1 with
    nc::MixRackChannel::toVar(). Keep the keys in sync with MixRack.cpp. */
export interface ChannelParams {
  stripOn: boolean;
  vocal: boolean;
  openMicOn: boolean;
  openMicUserSet: boolean;
  openMicDrive: number;
  openMicMix: number;
  openMicTrim: number;
  eqOn: boolean;
  hpfOn: boolean;
  hpfFreq: number;
  lpfOn: boolean;
  lpfFreq: number;
  lowFreq: number;
  lowGain: number;
  lmfFreq: number;
  lmfGain: number;
  lmfQ: number;
  hmfFreq: number;
  hmfGain: number;
  hmfQ: number;
  highFreq: number;
  highGain: number;
  compOn: boolean;
  compThresh: number;
  compRatio: number;
  compAttack: number;
  compRelease: number;
  compMakeup: number;
  satOn: boolean;
  satDrive: number;
  satMix: number;
  outputDb: number;
}

export type AutomationParam = "volume" | "pan";
export type AutomationCurve = "linear" | "step" | "smooth";

/** One breakpoint: clip-relative beats and a normalised 0..1 value. Pan is
    converted to the engine's -1..1 on the bridge, never in the surface. */
export interface AutomationPoint {
  beat: number;
  value: number;
}

export interface AutomationLaneDraft {
  param: AutomationParam;
  curve: AutomationCurve;
  points: AutomationPoint[];
}

/** A lane as the host publishes it. Same shape as a draft plus its track. */
export interface AutomationLaneState extends AutomationLaneDraft {
  track: number;
}

export interface PluginScanState {
  scanning: boolean;
  progress: number;
  status: string;
}

export interface AppState {
  bpm: number;
  selectedTrack: number;
  globalPitch?: PitchState;
  tracks: TrackInfo[];
  /** Last host status line (native setStatus); the strip falls back to it. */
  statusMessage?: string;
  statusAlert?: boolean;
  /** True while the host master chain holds a plugin. */
  hasMasterPlugin?: boolean;
  transport?: TransportState;
  /** Arrangement order, audio and MIDI together. Every clip route indexes it. */
  clips?: TimelineClip[];
  /** The full catalogue, including registry rows that a scan has not linked. */
  plugins?: PluginInfo[];
  /** Baked MixRack parameters for the selected track. */
  channel?: ChannelParams;
  /** Non-empty automation lanes across the session. */
  automation?: AutomationLaneState[];
  undoDepth?: number;
  redoDepth?: number;
  pluginScan?: PluginScanState;
}
