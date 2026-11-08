// GENERATED from console-ui — edit there, then run `npm run sync`.
import { Lcd } from "../Surface";
import { Scope } from "../Scope";
import { eqRenderer, vectorRenderer, waterfallRenderer } from "../renderers";
import { system } from "../tokens";
import { Grid, PAction, PChoice, PGroup, PKnob, PSlider, PToggle, PanelScope, Readout, Row, useHas, useLabel, useNum, useText, useValue, type PanelProps, type Schema } from "./controls";
import { Overlay, PluginFrame } from "./PluginFrame";
import { StereoMeter } from "./StereoMeter";

export const REVERB_QUALITY = ["Zero-lat", "NU 256", "NU 1024"] as const;

export const reverbSchema = {
  quality: { label: "Quality", def: 0.5, spec: { options: REVERB_QUALITY } },
  irStart: { label: "IR Start", def: 0, spec: { unit: "ms", min: 0, max: 500 } },
  irLength: { label: "IR Length", def: 1, spec: { unit: "%", min: 10, max: 100 } },
  predelay: { label: "Pre-Delay", def: 0.08, spec: { unit: "ms", min: 0, max: 250 } },
  earlyLate: { label: "Early / Late", def: 0.5, bipolar: true, spec: { unit: "%", min: 0, max: 100 } },
  modDepth: { label: "Mod Depth", def: 0, spec: { unit: "%", min: 0, max: 100 } },
  modRate: { label: "Mod Rate", def: 0.3, spec: { unit: "rate", min: 0.05, max: 5, curve: "log" } },
  syncPre: { label: "Sync Pre", def: 0 },
  reverse: { label: "Reverse", def: 0 },
  normalise: { label: "Normalise", def: 1 },
  lowCut: { label: "Low Cut", def: 0, spec: { unit: "Hz", min: 20, max: 1000, curve: "log" } },
  highCut: { label: "High Cut", def: 1, spec: { unit: "Hz", min: 1000, max: 20000, curve: "log" } },
  tilt: { label: "Tilt", def: 0.5, bipolar: true, spec: { unit: "dB", min: -6, max: 6 } },
  damping: { label: "HF Damping", def: 0.3, spec: { unit: "%", min: 0, max: 100 } },
  mix: { label: "Mix", def: 0.25, spec: { unit: "%", min: 0, max: 100 } },
  width: { label: "Width", def: 0.5, spec: { unit: "%", min: 0, max: 200 } },
  output: { label: "Output", def: 0.5, bipolar: true, spec: { unit: "dB", min: -24, max: 24 } },
  wetSolo: { label: "Wet Solo", def: 0 },
  freeze: { label: "Freeze", def: 0 },
} as const satisfies Schema;

export interface ReverbPanelProps extends PanelProps {
  /** Loaded impulse file name; null when none is loaded. */
  irName?: string | null;
  /** RT60 of the loaded IR in seconds (host-computed). */
  rt60?: number;
  /** Latency reported to the host, in samples. */
  latencySamples?: number;
}

function ReverbScreen({ irName, rt60, getFrame }: Pick<ReverbPanelProps, "irName" | "rt60" | "getFrame">) {
  const preMs = useNum("predelay");
  const lc = useValue("lowCut");
  const hc = useValue("highCut");
  const damp = useValue("damping");
  const len = useValue("irLength");
  const preText = useText("predelay");
  const widthText = useText("width");
  const tiltText = useText("tilt");
  const hasTilt = useHas("tilt");
  const quality = useLabel("quality");
  const hasQuality = useHas("quality");
  const decay = 0.12 + 0.3 * (rt60 ? Math.min(1, rt60 / 4) : 0.6) * (0.4 + 0.6 * len);
  return (
    <>
      <Scope renderer={waterfallRenderer} params={{ predelay: (Math.min(250, preMs) / 250) * 0.3, decay, hfDamp: Math.max(damp, 1 - hc), lowCut: lc }}
        width={1288} height={344} label={`Impulse decay by frequency${irName ? `, ${irName}` : ", no impulse loaded"}`} getFrame={getFrame as () => undefined} />
      <Overlay style={{ left: 26, top: 20, display: "flex", flexDirection: "column", gap: 2 }}>
        <Readout size={44} style={{ lineHeight: "46px" }}>{rt60 !== undefined ? <>{rt60.toFixed(2)}<span style={{ fontSize: 22 }}> s RT60</span></> : <span style={{ fontSize: 30 }}>NO IMPULSE</span>}</Readout>
        <Readout size={13} style={{ opacity: 0.8, letterSpacing: ".12em" }}>PRE {preText} · WIDTH {widthText}{hasTilt ? ` · TILT ${tiltText}` : ""}</Readout>
      </Overlay>
      <Overlay style={{ right: 24, top: 22, display: "flex", gap: 8 }}>
        <Lcd style={{ height: 30, fontSize: 13 }}>{irName ?? "Drop a .wav impulse"}</Lcd>
        {hasQuality && <Lcd style={{ height: 30, fontSize: 13 }}>{quality.toUpperCase()}</Lcd>}
      </Overlay>
    </>
  );
}

function ReverbDeck({ irName, meters }: Pick<ReverbPanelProps, "irName" | "meters">) {
  const width = useValue("width");
  const lcHz = useNum("lowCut");
  const hcHz = useNum("highCut");
  const tiltDb = useNum("tilt");
  return (
    <>
      <PGroup title="Impulse" chip={system.source} keys={["quality", "irStart", "irLength", "normalise"]} style={{ width: 300, boxSizing: "border-box" }}>
        <Row gap={8} align="stretch">
          <Lcd style={{ flexGrow: 1, minWidth: 0, height: 40, fontSize: 13, whiteSpace: "nowrap", overflow: "hidden" }}>{irName ?? "No impulse loaded"}</Lcd>
          <PAction name="loadIr" style={{ minHeight: 40 }}>Load</PAction>
        </Row>
        <PChoice k="quality" keyStyle={{ minHeight: 40 }} />
        <PSlider k="irStart" width={284} />
        <PSlider k="irLength" width={284} />
        <PToggle k="normalise" style={{ minHeight: 36 }} />
      </PGroup>

      <PGroup title="Time · Motion" chip={system.mod} keys={["predelay", "earlyLate", "modDepth", "modRate"]} style={{ width: 300, boxSizing: "border-box" }}>
        <Row justify="space-around">
          <PKnob k="predelay" size="xl" />
          <PKnob k="earlyLate" size="xl" />
        </Row>
        <Row justify="space-around">
          <PKnob k="modDepth" />
          <PKnob k="modRate" />
        </Row>
        <Grid cols={2}>
          <PToggle k="syncPre" style={{ minHeight: 36 }} />
          <PToggle k="reverse" style={{ minHeight: 36 }} />
        </Grid>
      </PGroup>

      <PGroup title="Tone" chip={system.filter} keys={["lowCut", "highCut", "tilt", "damping"]} style={{ width: 340, boxSizing: "border-box" }}>
        <Scope renderer={eqRenderer} width={308} height={120} label="Wet signal EQ"
          params={{ hpf: lcHz, range: 12, bands: [{ f: 1000, g: tiltDb, q: 0.3, t: "hs" }, { f: 1000, g: -tiltDb, q: 0.3, t: "ls" }, { f: hcHz, g: -12, q: 0.7, t: "hs" }] }} />
        <Row>
          <PKnob k="lowCut" />
          <PKnob k="highCut" />
          <PKnob k="tilt" />
        </Row>
        <PSlider k="damping" width={308} />
      </PGroup>

      <PGroup title="Space · Output" chip={system.out} keys={["mix", "width", "output"]} style={{ flexGrow: 1, minWidth: 0 }}>
        <Row>
          <PKnob k="mix" size="xl" />
          <PKnob k="width" />
          <PKnob k="output" />
        </Row>
        <Row gap={10} align="stretch">
          <Scope renderer={vectorRenderer} params={{ width: width * 1.2 }} width={150} height={118} label="Stereo field" />
          <div style={{ flexGrow: 1, display: "flex", flexDirection: "column", gap: 6 }}>
            <PToggle k="wetSolo" style={{ minHeight: 36 }} />
            <PToggle k="freeze" style={{ minHeight: 36 }} />
            <div style={{ marginTop: "auto" }}><StereoMeter read={meters?.out} segments={16} /></div>
          </div>
        </Row>
      </PGroup>
    </>
  );
}

export function ReverbPanel({ store, map, specs, presetName, onPresetStep, meters, getFrame, irName = null, rt60, latencySamples }: ReverbPanelProps) {
  return (
    <PanelScope schema={reverbSchema} specs={specs} store={store} map={map}>
      <PluginFrame name="REVERB" tag="OVERLAY FX · CONVOLUTION" subtitle="Your impulses · no shipped IRs" presetName={presetName} onPresetStep={onPresetStep}
        headerRight={latencySamples !== undefined && <Lcd style={{ height: 40, fontSize: 15, gap: 10 }}><span style={{ opacity: 0.6 }}>LATENCY</span>{latencySamples} SMP</Lcd>}
        screen={<ReverbScreen irName={irName} rt60={rt60} getFrame={getFrame} />}>
        <ReverbDeck irName={irName} meters={meters} />
      </PluginFrame>
    </PanelScope>
  );
}
