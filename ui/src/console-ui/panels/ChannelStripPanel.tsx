// GENERATED from console-ui — edit there, then run `npm run sync`.
import { useState } from "react";
import { Meter } from "../Meter";
import { Scope } from "../Scope";
import { Key, Seg, SidechainBadge } from "../Surface";
import { eqRenderer, historyRenderer, type EqBand } from "../renderers";
import { system } from "../tokens";
import {
  Grid,
  PChoice,
  PCycle,
  PGroup,
  PKnob,
  PToggle,
  PanelScope,
  Readout,
  Row,
  optionKey,
  useHas,
  useLabel,
  useNum,
  useValue,
  type PanelProps,
  type Schema,
} from "./controls";
import { Overlay, PluginFrame } from "./PluginFrame";
import { StereoMeter } from "./StereoMeter";

export const STRIP_SHAPES = ["Bell", "Shelf", "Notch", "BP", "AP"] as const;
export const STRIP_SLOPES = ["12", "24", "36", "48"] as const;
export const STRIP_TEMPLATES = [
  "Clean utility",
  "Vocal lead",
  "Keys glue",
  "Drum bus punch",
  "Bass tight",
  "Pad air",
  "Guitar bite",
  "Parallel smash",
] as const;

const hz = (min: number, max: number) => ({ unit: "Hz" as const, min, max, curve: "log" as const });
const BAND_DEFAULTS = [
  { f: 80, s: 1 },
  { f: 320, s: 0 },
  { f: 2400, s: 0 },
  { f: 10000, s: 1 },
];
const freqNorm = (f: number) => Math.log(f / 20) / Math.log(1000);

const bandSchema = Object.fromEntries(
  BAND_DEFAULTS.flatMap((b, i) => {
    const n = i + 1;
    return [
      [`band${n}Type`, { label: `Band ${n} shape`, def: b.s / 4, spec: { options: STRIP_SHAPES } }],
      [`band${n}Freq`, { label: "Freq", def: freqNorm(b.f), spec: hz(20, 20000) }],
      [
        `band${n}Gain`,
        { label: "Gain", def: 0.5, bipolar: true, spec: { unit: "dB", min: -18, max: 18 } },
      ],
      [
        `band${n}Q`,
        { label: "Q", def: 0.35, spec: { min: 0.1, max: 18, curve: "log", decimals: 2 } },
      ],
      [`band${n}Slope`, { label: `Band ${n} slope`, def: 0, spec: { options: STRIP_SLOPES } }],
      [`band${n}Dyn`, { label: "Dynamic", def: 0 }],
      [
        `band${n}DynThresh`,
        { label: "Dyn thresh", def: 0.6, spec: { unit: "dB", min: -60, max: 0 } },
      ],
      [
        `band${n}DynRange`,
        { label: "Dyn range", def: 0.3, bipolar: true, spec: { unit: "dB", min: -12, max: 12 } },
      ],
    ];
  }),
);

/** Maps a band-shape option label to what the EQ drawing needs. Accepts the
 *  panel's own labels and host labels ("Low Shelf", "Band Pass", ...). */
export function bandShape(label: string, band: number): "b" | "ls" | "hs" | "notch" | "bp" | "ap" {
  const k = optionKey(label);
  if (k === "shelf") return band <= 2 ? "ls" : "hs";
  if (k.startsWith("lo") && k.includes("sh")) return "ls";
  if (k.startsWith("hi") && k.includes("sh")) return "hs";
  if (k === "notch") return "notch";
  if (k === "bp" || k === "bandpass") return "bp";
  if (k === "ap" || k === "allpass") return "ap";
  return "b";
}

export const channelStripSchema = {
  eqOn: { label: "EQ", def: 1 },
  hpfOn: { label: "HPF", def: 1 },
  lpfOn: { label: "LPF", def: 1 },
  dynPreEq: { label: "Pre-EQ", def: 0 },
  compOn: { label: "Dynamics", def: 1 },
  compType: { label: "Character", def: 0, spec: { options: ["VCA", "FET", "Opto", "VariMu"] } },
  compKnee: { label: "Knee", def: 0.25, spec: { unit: "dB", min: 0, max: 24 } },
  compMakeup: {
    label: "Makeup",
    def: 0.33,
    bipolar: true,
    spec: { unit: "dB", min: -12, max: 24 },
  },
  compMix: { label: "Mix", def: 1, spec: { unit: "%", min: 0, max: 100 } },
  satMode: { label: "Sat mode", def: 0, spec: { options: ["Tape", "Tube", "Diode"] } },
  satBias: { label: "Bias", def: 0.5, bipolar: true, spec: { min: -1, max: 1, decimals: 2 } },
  satTone: { label: "Tone", def: 0.5, bipolar: true, spec: { min: -1, max: 1, decimals: 2 } },
  satOutput: { label: "Sat out", def: 0.5, bipolar: true, spec: { unit: "dB", min: -12, max: 12 } },
  satOversample: { label: "OS", def: 0 },
  trim: { label: "Trim", def: 0.5, bipolar: true, spec: { unit: "dB", min: -24, max: 24 } },
  phase: { label: "Ø Phase", def: 0 },
  hpfFreq: { label: "HPF", def: 0, spec: hz(20, 1000) },
  hpfSlope: { label: "HPF slope", def: 1 / 3, spec: { options: STRIP_SLOPES } },
  lpfFreq: { label: "LPF", def: 1, spec: hz(1000, 20000) },
  lpfSlope: { label: "LPF slope", def: 1 / 3, spec: { options: STRIP_SLOPES } },
  dynAttack: {
    label: "Dyn attack",
    def: 0.3,
    spec: { unit: "ms", min: 0.1, max: 100, curve: "log" },
  },
  dynRelease: {
    label: "Dyn release",
    def: 0.45,
    spec: { unit: "ms", min: 10, max: 2000, curve: "log" },
  },
  compThresh: { label: "Threshold", def: 1, spec: { unit: "dB", min: -60, max: 0 } },
  compRatio: { label: "Ratio", def: 0.23, spec: { unit: "ratio", min: 1, max: 20, curve: "log" } },
  compAttack: { label: "Attack", def: 0.3, spec: { unit: "ms", min: 0.1, max: 100, curve: "log" } },
  compRelease: {
    label: "Release",
    def: 0.5,
    spec: { unit: "ms", min: 10, max: 2000, curve: "log" },
  },
  compAmount: { label: "Amount", def: 1, spec: { unit: "%", min: 0, max: 100 } },
  compLookahead: { label: "Lookahead", def: 0, spec: { unit: "ms", min: 0, max: 10 } },
  compHold: { label: "Hold", def: 0, spec: { unit: "ms", min: 0, max: 200 } },
  compRange: { label: "Range", def: 1, spec: { unit: "dB", min: 0, max: 40 } },
  compAutoRelease: { label: "Auto Rel", def: 0 },
  compAutoGain: { label: "Auto Gain", def: 0 },
  scEq: { label: "SC EQ", def: 0 },
  scHpf: { label: "SC HPF", def: 0.2, spec: hz(20, 1000) },
  scExternal: { label: "Ext SC", def: 0 },
  satOn: { label: "Saturate", def: 0 },
  satDrive: { label: "Drive", def: 0.2, spec: { unit: "%", min: 0, max: 100 } },
  satMix: { label: "Mix", def: 1, spec: { unit: "%", min: 0, max: 100 } },
  width: { label: "Width", def: 0.5, spec: { unit: "%", min: 0, max: 200 } },
  mono: { label: "Mono", def: 0 },
  output: { label: "Output", def: 0.5, bipolar: true, spec: { unit: "dB", min: -24, max: 24 } },
  template: { label: "Template", def: 0, spec: { options: STRIP_TEMPLATES } },
  ...bandSchema,
} as Schema;

export interface ChannelStripPanelProps extends PanelProps {
  /** Exact EQ response from the DSP (dB at a frequency); falls back to an approximation. */
  eqCurve?: (hz: number) => number;
  /** Name of the external sidechain source when one is keyed. */
  sidechainSource?: string;
}

function useBands(): EqBand[] {
  const out: EqBand[] = [];
  const on = !useHas("eqOn") || useValue("eqOn") >= 0.5;
  for (let n = 1; n <= 4; n++) {
    // Fixed loop length: hook order is stable.
    const shape = bandShape(
      useLabel(`band${n}Type`) || STRIP_SHAPES[n === 1 || n === 4 ? 1 : 0],
      n,
    );
    const f = useNum(`band${n}Freq`);
    let g = useNum(`band${n}Gain`);
    const hasQ = useHas(`band${n}Q`);
    const qv = useNum(`band${n}Q`);
    const q = hasQ ? qv : 0.7;
    const has = useHas(`band${n}Freq`);
    if (!has || !on) continue;
    if (shape === "notch") g = -18;
    if (shape === "bp" || shape === "ap") g = shape === "bp" ? Math.max(g, 0) : 0;
    out.push({ f, g, q, t: shape === "ls" || shape === "hs" ? shape : "b" });
  }
  return out;
}

function StripScreen({
  sel,
  eqCurve,
  meters,
  sidechainSource,
  getFrame,
}: {
  sel: number;
  eqCurve?: (hz: number) => number;
  meters?: PanelProps["meters"];
  sidechainSource?: string;
  getFrame?: () => unknown;
}) {
  const bands = useBands();
  const hasHpf = useHas("hpfFreq");
  const hpfHz = useNum("hpfFreq");
  const hpfOn = !useHas("hpfOn") || useValue("hpfOn") >= 0.5;
  const hpf = hasHpf && hpfOn ? hpfHz : 0;
  const scOn = useValue("scExternal") >= 0.5;
  const frame = getFrame as
    (() => { spectrum?: ArrayLike<number>; gr?: number; level?: number } | undefined) | undefined;
  return (
    <div
      className="cs-screen"
      style={{ height: 330, boxSizing: "border-box", display: "flex", gap: 4 }}
    >
      <div style={{ position: "relative", flexShrink: 0 }}>
        <Scope
          renderer={eqRenderer}
          params={{ bands, hpf: hpf > 21 ? hpf : 0, selected: sel >= 4 ? -1 : sel, curve: eqCurve }}
          width={876}
          height={322}
          label="EQ curve over the post-EQ spectrum"
          getFrame={frame}
        />
        <Overlay style={{ left: 16, top: 12 }}>
          <Readout size={13} style={{ letterSpacing: ".12em" }}>
            EQUALISER · {sel >= 4 ? "HP · LP FILTERS" : `BAND ${sel + 1} SELECTED`}
          </Readout>
        </Overlay>
      </div>
      <div style={{ position: "relative", flexGrow: 1, minWidth: 0 }}>
        <Scope
          renderer={historyRenderer}
          params={{}}
          width={396}
          height={322}
          label="Gain reduction history"
          getFrame={
            frame
              ? () => ({ gr: frame()?.gr ?? meters?.gr?.() })
              : meters?.gr
                ? () => ({ gr: meters.gr!() })
                : undefined
          }
        />
        <Overlay style={{ left: 16, top: 12, display: "flex", flexDirection: "column", gap: 6 }}>
          <Readout size={13} style={{ letterSpacing: ".12em" }}>
            GAIN REDUCTION
          </Readout>
          {scOn && <SidechainBadge source={sidechainSource ?? "EXT"} />}
        </Overlay>
      </div>
    </div>
  );
}

const FILTER_PAGE = 4;

function StripDeck({
  sel,
  setSel,
  meters,
}: {
  sel: number;
  setSel: (n: number) => void;
  meters?: PanelProps["meters"];
}) {
  const bandHas = [1, 2, 3, 4].map((n) => useHas(`band${n}Freq`));
  const hasFilters = useHas("hpfFreq", "lpfFreq");
  const page = sel === FILTER_PAGE && hasFilters ? FILTER_PAGE : Math.min(sel, 3);
  const b = page + 1;
  const dynOn = useValue(`band${b}Dyn`) >= 0.5;
  const hasDynDetail = useHas(
    "compLookahead",
    "compHold",
    "compRange",
    "compAutoRelease",
    "compAutoGain",
    "scEq",
    "scExternal",
    "compAmount",
    "scHpf",
  );
  const [dynPage, setDynPage] = useState<"comp" | "detail">("comp");
  const dPage = hasDynDetail ? dynPage : "comp";
  return (
    <>
      <PGroup
        title="In"
        chip={system.out}
        keys={["trim", "phase"]}
        style={{ width: 120, boxSizing: "border-box", alignItems: "center" }}
      >
        <PKnob k="trim" />
        <PToggle k="phase" style={{ alignSelf: "stretch" }} />
        {meters?.in && (
          <div style={{ display: "flex", gap: 3, marginTop: "auto" }}>
            <Meter
              vertical
              read={() => meters.in!()[0]}
              segments={20}
              length={120}
              label="Input left"
            />
            <Meter
              vertical
              read={() => meters.in!()[1]}
              segments={20}
              length={120}
              label="Input right"
            />
          </div>
        )}
      </PGroup>

      <PGroup
        title="EQ"
        chip={system.filter}
        keys={["band1Freq", "band2Freq", "band3Freq", "band4Freq", "hpfFreq", "lpfFreq"]}
        enable="eqOn"
        style={{ width: 430, boxSizing: "border-box" }}
        extra={
          <div role="tablist" aria-label="EQ page" style={{ display: "flex", gap: 8 }}>
            {[0, 1, 2, 3].map(
              (i) =>
                bandHas[i] && (
                  <Key
                    key={i}
                    pressed={i === page}
                    onClick={() => setSel(i)}
                    label={`Band ${i + 1}`}
                  >
                    {i + 1}
                  </Key>
                ),
            )}
            {hasFilters && (
              <Key
                pressed={page === FILTER_PAGE}
                onClick={() => setSel(FILTER_PAGE)}
                label="High-pass and low-pass filters"
              >
                HP·LP
              </Key>
            )}
          </div>
        }
      >
        {page === FILTER_PAGE ? (
          <>
            <FilterRow on="hpfOn" freq="hpfFreq" slope="hpfSlope" />
            <FilterRow on="lpfOn" freq="lpfFreq" slope="lpfSlope" />
          </>
        ) : (
          <>
            <Row justify="space-around">
              <PKnob k={`band${b}Gain`} size="xl" />
              <PKnob k={`band${b}Freq`} size="l" />
              <PKnob k={`band${b}Q`} size="l" />
            </Row>
            <Row gap={8} align="stretch" justify="flex-start">
              <PChoice k={`band${b}Type`} variant="seg" style={{ flexGrow: 1 }} />
              <PCycle k={`band${b}Slope`} style={{ minWidth: 64 }} />
            </Row>
            <Row justify="space-between" align="center">
              <div style={{ display: "flex", flexDirection: "column", gap: 8, minWidth: 76 }}>
                <PToggle k={`band${b}Dyn`}>Dyn</PToggle>
                <PToggle k="dynPreEq">Pre-EQ</PToggle>
              </div>
              <PKnob k={`band${b}DynThresh`} size="m" off={!dynOn} />
              <PKnob k={`band${b}DynRange`} size="m" off={!dynOn} />
              <PKnob k="dynAttack" size="m" off={!dynOn} />
              <PKnob k="dynRelease" size="m" off={!dynOn} />
            </Row>
          </>
        )}
      </PGroup>

      <PGroup
        title="Dynamics"
        chip={system.dynamics}
        keys={["compThresh", "compRatio"]}
        enable="compOn"
        style={{ width: 340, boxSizing: "border-box" }}
        extra={
          hasDynDetail && (
            <Seg
              label="Dynamics page"
              value={dPage}
              onChange={setDynPage}
              options={[
                { id: "comp", label: "Comp" },
                { id: "detail", label: "More" },
              ]}
            />
          )
        }
      >
        {dPage === "comp" ? (
          <>
            <Row justify="space-around">
              <PKnob k="compThresh" size="xl" />
              <PKnob k="compRatio" size="l" />
              <PKnob k="compMakeup" size="l" />
            </Row>
            <Row justify="space-around">
              <PKnob k="compAttack" size="m" />
              <PKnob k="compRelease" size="m" />
              <PKnob k="compKnee" size="m" />
              <PKnob k="compMix" size="m" />
            </Row>
            <PChoice k="compType" variant="seg" />
          </>
        ) : (
          <>
            <Row justify="space-around">
              <PKnob k="compAmount" size="m" />
              <PKnob k="scHpf" size="m" />
              <PKnob k="compLookahead" size="m" label="Lookahead" />
              <PKnob k="compHold" size="m" />
              <PKnob k="compRange" size="m" />
            </Row>
            <Grid cols={2} gap={8}>
              <PToggle k="compAutoRelease" />
              <PToggle k="compAutoGain" />
              <PToggle k="scEq" />
              <PToggle k="scExternal">Ext SC</PToggle>
            </Grid>
          </>
        )}
      </PGroup>

      <PGroup
        title="Sat"
        chip={system.colour}
        keys={["satDrive", "satMix"]}
        enable="satOn"
        style={{ width: 200, boxSizing: "border-box" }}
        extra={<PToggle k="satOversample">OS</PToggle>}
      >
        <Row justify="space-around">
          <PKnob k="satDrive" size="xl" learn />
          <PKnob k="satMix" size="l" learn />
        </Row>
        <PChoice k="satMode" variant="seg" />
        <Row justify="space-around">
          <PKnob k="satBias" size="m" learn />
          <PKnob k="satTone" size="m" learn />
          <PKnob k="satOutput" size="m" label="Out" learn />
        </Row>
      </PGroup>

      <PGroup
        title="Out"
        chip={system.out}
        keys={["output", "width"]}
        style={{ flexGrow: 1, minWidth: 0, alignItems: "center" }}
      >
        <Row gap={10} justify="center">
          <PKnob k="output" size="xl" />
          <PKnob k="width" />
        </Row>
        <PToggle k="mono" style={{ alignSelf: "stretch" }} />
        <PCycle k="template" style={{ alignSelf: "stretch" }} />
        <StereoMeter read={meters?.out} segments={18} />
      </PGroup>
    </>
  );
}

/** One cut filter on the HP·LP page: on key, big frequency knob, slope keys. */
function FilterRow({ on, freq, slope }: { on: string; freq: string; slope: string }) {
  const has = useHas(freq);
  const enabled = !useHas(on) || useValue(on) >= 0.5;
  if (!has) return null;
  return (
    <Row gap={16} align="center" justify="flex-start">
      <PToggle k={on} style={{ minWidth: 64 }} />
      <PKnob k={freq} size="xl" off={!enabled} />
      <div style={{ flexGrow: 1, display: "flex", flexDirection: "column", gap: 6 }}>
        <span className="cs-sub">Slope dB/oct</span>
        <PChoice k={slope} variant="seg" />
      </div>
    </Row>
  );
}

export function ChannelStripPanel({
  store,
  map,
  specs,
  presetName,
  onPresetStep,
  meters,
  eqCurve,
  sidechainSource,
  getFrame,
}: ChannelStripPanelProps) {
  const [sel, setSel] = useState(2);
  return (
    <PanelScope schema={channelStripSchema} specs={specs} store={store} map={map}>
      <PluginFrame
        name="CHANNEL STRIP"
        tag="OVERLAY FX"
        subtitle="In → EQ → Dynamics → Saturate → Out"
        presetName={presetName}
        onPresetStep={onPresetStep}
        screenHeight={330}
        screen={
          <StripScreen
            sel={sel}
            eqCurve={eqCurve}
            meters={meters}
            sidechainSource={sidechainSource}
            getFrame={getFrame}
          />
        }
      >
        <StripDeck sel={sel} setSel={setSel} meters={meters} />
      </PluginFrame>
    </PanelScope>
  );
}
