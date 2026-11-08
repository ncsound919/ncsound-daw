// GENERATED from console-ui — edit there, then run `npm run sync`.
import { Lcd } from "../Surface";
import { Scope } from "../Scope";
import { eqRenderer, transferRenderer, waveRenderer } from "../renderers";
import { system } from "../tokens";
import { PAction, PChoice, PCycle, PGroup, PKnob, PSlider, PToggle, PanelScope, Readout, Row, useHas, useLabel, useNum, useOption, useValue, type PanelProps, type Schema } from "./controls";
import { Overlay, PluginFrame } from "./PluginFrame";
import { StereoMeter } from "./StereoMeter";

// Keys are the native Overlay Lo-Fi parameter ids (identity panelMap). Specs
// here only drive the preview; in the plugin the host's ranges/skews win.
export const LOFI_NOISE_TYPES = ["Vinyl", "Hiss", "Hum 60", "Crackle", "Sample", "Hum 50", "Tape"] as const;
export const LOFI_DISTORT_MODES = ["Tape", "Tube", "Fold", "Clip", "Fuzz", "Diode"] as const;

const pct = { unit: "%", min: 0, max: 100 } as const;
export const lofiSchema = {
  noiseOn: { label: "Noise", def: 0 },
  noiseType: { label: "Noise type", def: 0, spec: { options: LOFI_NOISE_TYPES } },
  noiseLevel: { label: "Level", def: 0.25, spec: pct },
  noiseTone: { label: "Tone", def: 0.5, bipolar: true, spec: { unit: "%", min: -100, max: 100 } },
  noiseFollow: { label: "Follow", def: 0, spec: pct },
  noiseDuck: { label: "Duck", def: 0, spec: pct },
  noisePost: { label: "Post", def: 0 },

  wobbleOn: { label: "Wobble", def: 0 },
  wobbleDepth: { label: "Amount", def: 0.35, spec: pct },
  wobbleBalance: { label: "Wow ⇄ Flutter", def: 0.5, spec: pct },
  wobbleRate: { label: "Rate", def: 0.3, spec: { unit: "Hz", min: 0.1, max: 10, curve: "log" } },
  wobbleStereo: { label: "Stereo", def: 0.5, spec: pct },
  wobbleMix: { label: "Mix", def: 1, spec: pct },

  distortOn: { label: "Distort", def: 0 },
  distortMode: { label: "Type", def: 0, spec: { options: LOFI_DISTORT_MODES } },
  distortDrive: { label: "Amount", def: 1 / 3, spec: { unit: "dB", min: 0, max: 24 } },
  distortLo: { label: "Focus lo", def: 0.5, spec: { unit: "Hz", min: 20, max: 2000, curve: "log" } },
  distortHi: { label: "Focus hi", def: 0.7, spec: { unit: "Hz", min: 200, max: 20000, curve: "log" } },
  distortMix: { label: "Mix", def: 0.5, spec: pct },

  digitalOn: { label: "Digital", def: 0 },
  digitalRate: { label: "Rate", def: 0.3, spec: pct },
  digitalBits: { label: "Bits", def: 0.3, spec: pct },
  digitalBalance: { label: "Rate ⇄ Bits", def: 0.5, spec: pct },
  digitalSmooth: { label: "Smooth", def: 0.3, spec: pct },
  digitalMix: { label: "Mix", def: 0.5, spec: pct },
  digitalLo: { label: "Focus lo", def: 0, spec: { unit: "Hz", min: 20, max: 2000, curve: "log" } },
  digitalHi: { label: "Focus hi", def: 1, spec: { unit: "Hz", min: 200, max: 20000, curve: "log" } },
  digitalCut: { label: "Cut", def: 0 },

  spaceOn: { label: "Space", def: 0 },
  spaceMix: { label: "Dry/Wet", def: 0.25, spec: pct },
  spaceDecay: { label: "Decay", def: 0.5, spec: pct },
  spacePredelay: { label: "Predelay", def: 0.1, spec: { unit: "ms", min: 0, max: 200 } },
  spaceDamping: { label: "Damping", def: 0.5, spec: pct },
  spaceWidth: { label: "Stereo", def: 0.8, spec: pct },

  magneticOn: { label: "Magnetic", def: 0 },
  magneticMix: { label: "Amount", def: 0.5, spec: pct },
  magneticWear: { label: "Wear", def: 0.4, spec: pct },
  magneticFlutter: { label: "Flutter", def: 0.3, spec: pct },
  magneticDropouts: { label: "Dropouts", def: 0, spec: pct },
  magneticStereo: { label: "Stereo", def: 1 },

  magnitude: { label: "Magnitude", def: 0.5, spec: { unit: "%", min: 0, max: 200 } },
  flux: { label: "Flux", def: 0, spec: pct },
  inputGain: { label: "Input", def: 0.5, bipolar: true, spec: { unit: "dB", min: -24, max: 24 } },
  output: { label: "Output", def: 0.5, bipolar: true, spec: { unit: "dB", min: -24, max: 24 } },
  width: { label: "Width", def: 0.5, spec: { unit: "%", min: 0, max: 200 } },
  eqOn: { label: "EQ", def: 1 },
  tone: { label: "Tone", def: 0.5, bipolar: true, spec: { unit: "%", min: -100, max: 100 } },
  toneMode: { label: "Tone mode", def: 0, spec: { options: ["Tilt", "Mid"] } },
  lowCut: { label: "Low cut", def: 0, spec: { unit: "Hz", min: 20, max: 1000, curve: "log" } },
  highCut: { label: "High cut", def: 1, spec: { unit: "Hz", min: 1000, max: 20000, curve: "log" } },
  cutSlope: { label: "Slope", def: 0, spec: { options: ["Soft", "Hard"] } },
} as const satisfies Schema;

export interface LoFiPanelProps extends PanelProps {
  /** Name of the loaded noise sample; undefined = the host has no sample loader. */
  noiseSampleName?: string | null;
  /** Loaded noise sample, one absolute peak per bucket (0..1). */
  noisePeaks?: () => ArrayLike<number> | undefined;
}

/* RBJ biquad magnitude, same formulas as fx::BiquadCoeffs (48 kHz reference). */
type Bq = "lp" | "hp" | "peak" | "ls" | "hs";
function bqDb(type: Bq, f0: number, q: number, gainDb: number, f: number, sr = 48000): number {
  const w0 = (2 * Math.PI * Math.min(f0, sr * 0.49)) / sr, cw = Math.cos(w0), sw = Math.sin(w0), al = sw / (2 * q);
  const A = Math.pow(10, gainDb / 40), sA = Math.sqrt(A);
  let b0 = 1, b1 = 0, b2 = 0, a0 = 1, a1 = 0, a2 = 0;
  if (type === "lp") { b0 = (1 - cw) / 2; b1 = 1 - cw; b2 = b0; a0 = 1 + al; a1 = -2 * cw; a2 = 1 - al; }
  else if (type === "hp") { b0 = (1 + cw) / 2; b1 = -(1 + cw); b2 = b0; a0 = 1 + al; a1 = -2 * cw; a2 = 1 - al; }
  else if (type === "peak") { b0 = 1 + al * A; b1 = -2 * cw; b2 = 1 - al * A; a0 = 1 + al / A; a1 = -2 * cw; a2 = 1 - al / A; }
  else if (type === "ls") { b0 = A * ((A + 1) - (A - 1) * cw + 2 * sA * al); b1 = 2 * A * ((A - 1) - (A + 1) * cw); b2 = A * ((A + 1) - (A - 1) * cw - 2 * sA * al); a0 = (A + 1) + (A - 1) * cw + 2 * sA * al; a1 = -2 * ((A - 1) + (A + 1) * cw); a2 = (A + 1) + (A - 1) * cw - 2 * sA * al; }
  else { b0 = A * ((A + 1) + (A - 1) * cw + 2 * sA * al); b1 = -2 * A * ((A - 1) + (A + 1) * cw); b2 = A * ((A + 1) + (A - 1) * cw - 2 * sA * al); a0 = (A + 1) - (A - 1) * cw + 2 * sA * al; a1 = 2 * ((A - 1) - (A + 1) * cw); a2 = (A + 1) - (A - 1) * cw - 2 * sA * al; }
  const w = (2 * Math.PI * f) / sr, c1 = Math.cos(w), s1 = Math.sin(w), c2 = Math.cos(2 * w), s2 = Math.sin(2 * w);
  const nr = b0 + b1 * c1 + b2 * c2, ni = -(b1 * s1 + b2 * s2), dr = a0 + a1 * c1 + a2 * c2, di = -(a1 * s1 + a2 * s2);
  return 10 * Math.log10((nr * nr + ni * ni) / (dr * dr + di * di));
}

/** Master EQ response from the current settings (the same filters the DSP runs). */
function useMasterCurve(): ((hz: number) => number) | undefined {
  const eqVal = useValue("eqOn");
  const hasEq = useHas("eqOn", "tone");
  const on = eqVal >= 0.5 && hasEq;
  const tone = useNum("tone") / 100;
  const mid = useOption("toneMode") === 1;
  const lo = useNum("lowCut"), hi = useNum("highCut");
  const stages = useOption("cutSlope") === 1 ? 2 : 1;
  const hasCuts = useHas("lowCut", "highCut");
  if (!on) return () => 0;
  return (f: number) => {
    let db = mid ? bqDb("peak", 1000, 0.6, -tone * 6, f) : bqDb("ls", 250, 0.707, -tone * 6, f) + bqDb("hs", 4000, 0.707, tone * 6, f);
    if (hasCuts) {
      if (lo > 20.5) db += stages * bqDb("hp", lo, 0.707, 0, f);
      if (hi < 19900) db += stages * bqDb("lp", hi, 0.707, 0, f);
    }
    return Math.max(-40, db);
  };
}

const MODULES: [string, string][] = [["noiseOn", "NOISE"], ["wobbleOn", "WOBBLE"], ["distortOn", "DISTORT"], ["digitalOn", "DIGITAL"], ["spaceOn", "SPACE"], ["magneticOn", "MAGNETIC"]];

function useModuleOn(k: string): boolean {
  const v = useValue(k), has = useHas(k);
  return has && v >= 0.5;
}

function ChainReadout() {
  const states = [useModuleOn("noiseOn"), useModuleOn("wobbleOn"), useModuleOn("distortOn"), useModuleOn("digitalOn"), useModuleOn("spaceOn"), useModuleOn("magneticOn")];
  const on = MODULES.filter((_, i) => states[i]).map(([, n]) => n);
  return <Readout size={15}>{on.length ? on.join(" → ") : "ALL MODULES OFF"}</Readout>;
}

function LoFiScreen({ getFrame, meters, noiseSampleName, noisePeaks }: Pick<LoFiPanelProps, "getFrame" | "meters" | "noiseSampleName" | "noisePeaks">) {
  const curve = useMasterCurve();
  const distVal = useValue("distortOn"), hasDist = useHas("distortOn");
  const digVal = useValue("digitalOn"), hasDig = useHas("digitalOn");
  const distOn = distVal >= 0.5 && hasDist, digOn = digVal >= 0.5 && hasDig;
  const mode = useOption("distortMode");
  const driveDb = useNum("distortDrive");
  const mag = useNum("magnitude") / 100;
  const distMix = Math.min(1, (useNum("distortMix") / 100) * mag);
  const digMix = Math.min(1, (useNum("digitalMix") / 100) * mag);
  // Same mapping as the DSP: bits amount = bits * balance, levels = 2^max(1, 16 - 14 * amount).
  const bitAmt = (useNum("digitalBits") / 100) * (useNum("digitalBalance") / 100);
  const levels = Math.pow(2, Math.max(1, 16 - bitAmt * 14));
  const typeLabel = useLabel("noiseType") || LOFI_NOISE_TYPES[0];
  const noiseVal = useValue("noiseOn"), hasNoise = useHas("noiseOn");
  const noiseOn = noiseVal >= 0.5 && hasNoise;
  const isSample = typeLabel === "Sample";
  const frame = getFrame as (() => { spectrum?: ArrayLike<number> } | undefined) | undefined;
  const peaks = noisePeaks?.();
  const hasLoader = noiseSampleName !== undefined;
  const hasPeaks = !!peaks && peaks.length > 0;
  return (
    <div className="cs-screen" style={{ height: 230, boxSizing: "border-box", display: "flex", gap: 4 }}>
      <div style={{ position: "relative", flexShrink: 0 }}>
        <Scope renderer={eqRenderer} params={{ bands: [], curve }} width={740} height={222} getFrame={frame}
          label={frame ? "Output spectrum with the master EQ curve" : "Master EQ curve (preview spectrum)"} />
        <Overlay style={{ left: 16, top: 12, display: "flex", flexDirection: "column", gap: 2 }}>
          <Readout size={11} style={{ letterSpacing: ".12em", opacity: 0.75 }}>SIGNAL CHAIN</Readout>
          <ChainReadout />
        </Overlay>
      </div>
      <div style={{ position: "relative", flexShrink: 0 }}>
        <Scope renderer={transferRenderer} params={{ distOn, mode, driveDb, distMix, digOn, levels, digMix }} width={222} height={222}
          label="Static transfer curve of Distort and the Digital quantiser" />
        <Overlay style={{ left: 12, top: 10 }}><Readout size={11} style={{ letterSpacing: ".12em", opacity: 0.75 }}>CHARACTER</Readout></Overlay>
      </div>
      <div style={{ position: "relative", flexGrow: 1, minWidth: 0 }}>
        <Scope renderer={waveRenderer} params={{ peaks: hasPeaks ? Array.from(peaks!) : [], loopStart: 0, loopEnd: 1, demoMotion: false }} width={230} height={222}
          label={hasPeaks ? `Loaded noise sample ${noiseSampleName ?? ""}` : "No noise sample loaded"} style={{ opacity: noiseOn && isSample ? 1 : 0.55 }} />
        <Overlay style={{ left: 12, top: 10, display: "flex", flexDirection: "column", gap: 2 }}>
          <Readout size={11} style={{ letterSpacing: ".12em", opacity: 0.75 }}>NOISE SOURCE</Readout>
          <Readout size={15}>{noiseOn ? typeLabel.toUpperCase() : "NOISE OFF"}</Readout>
        </Overlay>
        {hasLoader && (
          <Overlay interactive style={{ left: 10, right: 10, bottom: 10, display: "flex", gap: 8, alignItems: "center" }}>
            <Lcd style={{ flexGrow: 1, minWidth: 0, height: 36, fontSize: 11, overflow: "hidden", whiteSpace: "nowrap" }}>{noiseSampleName || "No sample"}</Lcd>
            <PAction name="loadNoise" style={{ minHeight: 36 }}>Load</PAction>
          </Overlay>
        )}
      </div>
      <div style={{ width: 70, flexShrink: 0, display: "flex", flexDirection: "column", justifyContent: "center", gap: 10, padding: "0 4px" }}>
        {meters?.in && <><Readout size={11} style={{ opacity: 0.75 }}>IN</Readout><StereoMeter read={meters.in} segments={10} label="Input level" /></>}
        {meters?.out && <><Readout size={11} style={{ opacity: 0.75 }}>OUT</Readout><StereoMeter read={meters.out} segments={10} label="Output level" /></>}
      </div>
    </div>
  );
}

const MOD_W = 330;

function Module({ title, chip, onKey, keys, children }: { title: string; chip: string; onKey: string; keys: string[]; children: React.ReactNode }) {
  return (
    <PGroup title={title} chip={chip} keys={keys} enable={onKey} style={{ width: MOD_W, boxSizing: "border-box" }}>
      <div style={{ display: "flex", flexDirection: "column", gap: 8 }}>{children}</div>
    </PGroup>
  );
}

function LoFiDeck() {
  return (
    <div style={{ display: "flex", gap: 14, width: "100%" }}>
      <div style={{ display: "grid", gridTemplateColumns: `repeat(3, ${MOD_W}px)`, gridAutoRows: "min-content", gap: 12, alignContent: "start" }}>
        <Module title="Noise" chip={system.source} onKey="noiseOn" keys={["noiseType", "noiseLevel", "noiseTone"]}>
          <Row align="center"><PCycle k="noiseType" style={{ flexGrow: 1 }} /><PToggle k="noisePost" style={{ minHeight: 36, padding: "0 12px" }}>Post</PToggle></Row>
          <Row><PKnob k="noiseLevel" size="xl" /><PKnob k="noiseTone" size="m" /><PKnob k="noiseFollow" size="m" /><PKnob k="noiseDuck" size="m" /></Row>
        </Module>
        <Module title="Wobble" chip={system.mod} onKey="wobbleOn" keys={["wobbleDepth", "wobbleBalance", "wobbleRate"]}>
          <Row><PKnob k="wobbleDepth" size="xl" /><PKnob k="wobbleRate" size="m" /><PKnob k="wobbleStereo" size="m" /><PKnob k="wobbleMix" size="m" /></Row>
          <PSlider k="wobbleBalance" width={298} />
        </Module>
        <Module title="Distort" chip={system.colour} onKey="distortOn" keys={["distortMode", "distortDrive", "distortMix"]}>
          <PCycle k="distortMode" />
          <Row><PKnob k="distortDrive" size="xl" /><PKnob k="distortLo" size="m" /><PKnob k="distortHi" size="m" /><PKnob k="distortMix" size="m" /></Row>
        </Module>
        <Module title="Digital" chip={system.filter} onKey="digitalOn" keys={["digitalRate", "digitalBits", "digitalMix"]}>
          <Row><PKnob k="digitalRate" size="l" /><PKnob k="digitalBits" size="l" /><PKnob k="digitalSmooth" size="m" /><PKnob k="digitalMix" size="m" /></Row>
          <Row align="center"><PSlider k="digitalBalance" width={112} /><PKnob k="digitalLo" size="s" /><PKnob k="digitalHi" size="s" /><PToggle k="digitalCut" style={{ minHeight: 36, padding: "0 10px" }} /></Row>
        </Module>
        <Module title="Space" chip={system.env} onKey="spaceOn" keys={["spaceMix", "spaceDecay", "spacePredelay"]}>
          <Row><PKnob k="spaceMix" size="xl" /><PKnob k="spaceDecay" size="m" /><PKnob k="spacePredelay" size="m" /><PKnob k="spaceDamping" size="m" /></Row>
          <PSlider k="spaceWidth" width={298} />
        </Module>
        <Module title="Magnetic" chip={system.out} onKey="magneticOn" keys={["magneticMix", "magneticWear", "magneticDropouts"]}>
          <Row><PKnob k="magneticMix" size="xl" /><PKnob k="magneticWear" size="m" /><PKnob k="magneticFlutter" size="m" /><PKnob k="magneticDropouts" size="m" /></Row>
          <Row justify="flex-end"><PToggle k="magneticStereo" style={{ minHeight: 36, padding: "0 12px" }} /></Row>
        </Module>
      </div>
      <PGroup title="Master" chip={system.out} keys={["magnitude", "flux", "output", "tone"]} style={{ flexGrow: 1, minWidth: 0, boxSizing: "border-box" }}>
        <div style={{ display: "flex", flexDirection: "column", gap: 8 }}>
          <Row justify="space-around"><PKnob k="magnitude" size="xl" /><PKnob k="flux" size="l" /></Row>
          <Row justify="space-around"><PKnob k="inputGain" size="m" /><PKnob k="width" size="m" /><PKnob k="output" size="m" /></Row>
          <Row align="center"><PToggle k="eqOn" style={{ minHeight: 36, padding: "0 12px" }} /><PChoice k="toneMode" variant="seg" style={{ flexGrow: 1 }} /></Row>
          <Row><PKnob k="tone" size="m" /><PKnob k="lowCut" size="m" /><PKnob k="highCut" size="m" /></Row>
          <PChoice k="cutSlope" variant="seg" />
        </div>
      </PGroup>
    </div>
  );
}

export function LoFiPanel({ store, map, specs, presetName, onPresetStep, meters, getFrame, noiseSampleName, noisePeaks }: LoFiPanelProps) {
  return (
    <PanelScope schema={lofiSchema} specs={specs} store={store} map={map}>
      <PluginFrame name="LO-FI" tag="OVERLAY FX · RETRO COLOR" subtitle="Noise · wobble · distort · digital · space · magnetic" presetName={presetName} onPresetStep={onPresetStep}
        screenHeight={230} screen={<LoFiScreen getFrame={getFrame} meters={meters} noiseSampleName={noiseSampleName} noisePeaks={noisePeaks} />}>
        <LoFiDeck />
      </PluginFrame>
    </PanelScope>
  );
}
