// GENERATED from console-ui — edit there, then run `npm run sync`.
import type { ComponentType } from "react";
import type { PanelProps, Schema } from "./controls";
import { DelayPanel, delaySchema } from "./DelayPanel";
import { ReverbPanel, reverbSchema } from "./ReverbPanel";
import { ModulatorPanel, modulatorSchema } from "./ModulatorPanel";
import { VibeBoxPanel, vibeboxSchema } from "./VibeBoxPanel";
import { LoFiPanel, lofiSchema } from "./LoFiPanel";
import { MasterPanel, masterSchema } from "./MasterPanel";
import { ChannelStripPanel, channelStripSchema } from "./ChannelStripPanel";

export * from "./controls";
export { PluginFrame, Overlay, PANEL_W, PANEL_H } from "./PluginFrame";
export { StereoMeter } from "./StereoMeter";
export { DelayPanel, delaySchema, DELAY_MODES, DELAY_TOPOLOGIES, DELAY_DIVISIONS } from "./DelayPanel";
export { ReverbPanel, reverbSchema, REVERB_QUALITY, type ReverbPanelProps } from "./ReverbPanel";
export { ModulatorPanel, modulatorSchema, SHAPERS, SHAPER_RATES, SHAPER_DEFAULT_CURVES, MOD_LFO_WAVES, SNAP_STEPS, type ModulatorFrame, type ShaperDef } from "./ModulatorPanel";
export { VibeBoxPanel, vibeboxSchema, VIBE_BITS, VIBE_RPM, VIBE_CONSOLES, VIBE_MACHINES, type VibeBoxPanelProps } from "./VibeBoxPanel";
export { LoFiPanel, lofiSchema, LOFI_NOISE_TYPES, LOFI_DISTORT_MODES, type LoFiPanelProps } from "./LoFiPanel";
export { MasterPanel, masterSchema, MASTER_TARGETS, MASTER_TARGET_NOTES, MASTER_MODULES, type MasterPanelProps, type MasterFrame, type MasterExtra, type MasterReference, type MasterModuleKey } from "./MasterPanel";
export { ChannelStripPanel, channelStripSchema, STRIP_SHAPES, STRIP_SLOPES, STRIP_TEMPLATES, type ChannelStripPanelProps } from "./ChannelStripPanel";

export interface PanelEntry {
  Panel: ComponentType<PanelProps & Record<string, unknown>>;
  /** Extra preview-only props (loaded IR name, loudness readings...). */
  props?: Record<string, unknown>;
  schema: Schema;
  /** One-shot actions the panel can show (hidden unless the host supports them). */
  actions: string[];
  /** Preview-only values that show the panel in a representative state. */
  preview?: Record<string, number>;
  presetName: string;
}

export const panels: Record<string, PanelEntry> = {
  delay: {
    Panel: DelayPanel, schema: delaySchema, actions: ["tap", "halve", "double", "clear"], presetName: "DUB TAPE THROW",
    preview: { feedback: 0.56, drive: 0.3, diffusion: 0.4, loopMod: 0.2, duck: 0.55, age: 0.45, wow: 0.35, lowCut: 0.25, highCut: 0.62, mix: 0.32, width: 0.6 },
  },
  reverb: {
    Panel: ReverbPanel as PanelEntry["Panel"], schema: reverbSchema, actions: ["loadIr"], presetName: "KEYS · STONE ROOM",
    preview: { predelay: 0.12, earlyLate: 0.55, modDepth: 0.25, lowCut: 0.35, highCut: 0.55, tilt: 0.42, damping: 0.45, mix: 0.24, width: 0.7, syncPre: 1 },
    props: { irName: "Stone_Room_2.4s.wav", rt60: 2.41, latencySamples: 256 },
  },
  modulator: {
    Panel: ModulatorPanel as PanelEntry["Panel"], schema: modulatorSchema, actions: ["resetCurves"], presetName: "PUMP 1/4",
    preview: { shVolOn: 1, shFltOn: 1, shFltRate: 3 / 19, shPanOn: 1, shPanRate: 5 / 19, chorusOn: 1, tremOn: 0 },
  },
  vibebox: {
    Panel: VibeBoxPanel as PanelEntry["Panel"], schema: vibeboxSchema, actions: ["dice", "loadNoise"], presetName: "DUSTY 12-BIT KEYS",
    preview: { magnitude: 0.58, mix: 0.85, bits: 3 / 8, rate: 0.55, jitter: 0.08, dither: 1, crackle: 0.4, wear: 0.4, wow: 0.25, dust: 0.22, rumble: 0.15, drive: 0.45, crosstalk: 0.2, consoleNoise: 0.15, tolerance: 0.3, machineAmount: 0.6, machineAge: 0.35, pitchDown: 0.33 },
    props: { noiseSampleName: "crackle_rec_01.wav" },
  },
  lofi: {
    Panel: LoFiPanel as PanelEntry["Panel"], schema: lofiSchema, actions: ["loadNoise"], presetName: "BROKEN CASSETTE",
    preview: { noiseOn: 1, noiseType: 1 / 6, noiseLevel: 0.18, noiseTone: 0.6, noiseFollow: 0.5, wobbleOn: 1, wobbleDepth: 0.4, wobbleRate: 0.33, wobbleStereo: 0.6, distortOn: 1, distortMode: 1 / 5, distortDrive: 0.45, distortMix: 0.4, digitalOn: 0, spaceOn: 0, magneticOn: 1, magneticWear: 0.5, magneticFlutter: 0.4, magneticDropouts: 0.2, magnitude: 0.6, tone: 0.4, lowCut: 0.25, highCut: 0.8 },
    props: { noiseSampleName: "" },
  },
  master: {
    Panel: MasterPanel as PanelEntry["Panel"], schema: masterSchema, actions: ["loadReference", "resetLoudness"], presetName: "LOUD MODERN",
    preview: { eqOn: 1, eq2Gain: 0.56, eq3Gain: 0.44, eq7Gain: 0.56, eq4Ch: 0.5, eq4Gain: 0.58, dynOn: 1, maxOn: 1, maxThresh: 2 / 3, maxMode: 1, maxTransient: 0.3, imgOn: 1, "img4Width": 0.65 },
  },
  strip: {
    Panel: ChannelStripPanel as PanelEntry["Panel"], schema: channelStripSchema, actions: [], presetName: "KEYS · WARM LIFT",
    preview: { band1Gain: 0.56, band2Gain: 0.41, band2Dyn: 1, band3Gain: 0.55, band3Q: 0.45, band4Gain: 0.57, hpfFreq: 0.1, compThresh: 0.6, compRatio: 0.3, compLookahead: 0.2, compAutoRelease: 1, compAutoGain: 1, scExternal: 1, satOn: 0, satDrive: 0.22, satMix: 0.6, width: 0.55, output: 0.53, template: 2 / 7 },
    props: { sidechainSource: "KICK" },
  },
};

export { SamplerPanel, samplerProgramSchema, PAD_FILTERS, type SamplerPanelProps, type SamplerState, type SamplerHost, type SamplerMode, type ChopSettings, type ChopMode, type TrimSettings, type TrimOp, type LayerInfo } from "./SamplerPanel";
export { Pads, BANKS, padLabel, type PadInfo } from "./Pads";
