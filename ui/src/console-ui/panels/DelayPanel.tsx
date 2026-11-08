// GENERATED from console-ui — edit there, then run `npm run sync`.
import { Scope } from "../Scope";
import { Led, SidechainBadge, Lcd } from "../Surface";
import { tapsRenderer } from "../renderers";
import { system } from "../tokens";
import {
  Grid,
  PAction,
  PChoice,
  PCycle,
  PGroup,
  PKnob,
  PSlider,
  PToggle,
  PanelScope,
  Readout,
  Row,
  optionKey,
  useHas,
  useLabel,
  useNum,
  useOption,
  useText,
  useValue,
  type PanelProps,
  type Schema,
} from "./controls";
import { Overlay, PluginFrame } from "./PluginFrame";
import { RhythmEditor } from "./RhythmEditor";
import { StereoMeter } from "./StereoMeter";

export const DELAY_MODES = ["Digital", "Tape", "BBD", "Pitch", "Reverse", "Ghost"] as const;
export const DELAY_TOPOLOGIES = ["Single", "Dual", "Ping-Pong", "Ratio", "Rhythm"] as const;
export const DELAY_DIVISIONS = [
  "1/64",
  "1/32T",
  "1/32",
  "1/32D",
  "1/16T",
  "1/16",
  "1/16D",
  "1/8T",
  "1/8",
  "1/8D",
  "1/4T",
  "1/4",
  "1/4D",
  "1/2T",
  "1/2",
  "1/2D",
  "1/1T",
  "1/1",
  "1/1D",
  "2/1",
] as const;
export const DELAY_RATIOS = [
  "1:2",
  "2:3",
  "3:4",
  "4:5",
  "1:1",
  "5:4",
  "4:3",
  "3:2",
  "2:1",
] as const;
export const DELAY_SATURATION = ["Tape", "Tube", "Diode"] as const;

export const delaySchema = {
  mode: { label: "Mode", def: 1 / 5, spec: { options: DELAY_MODES } },
  age: { label: "Age", def: 0.3, spec: { unit: "%", min: 0, max: 100 } },
  wow: { label: "Wow", def: 0.2, spec: { unit: "%", min: 0, max: 100 } },
  flutter: { label: "Flutter", def: 0.15, spec: { unit: "%", min: 0, max: 100 } },
  sync: { label: "Sync", def: 1 },
  division: { label: "Time", def: 8 / 19, spec: { options: DELAY_DIVISIONS } },
  timeMs: { label: "Time", def: 0.35, spec: { unit: "ms", min: 1, max: 2000, curve: "log" } },
  feedback: { label: "Feedback", def: 0.45, spec: { unit: "%", min: 0, max: 110 } },
  topology: { label: "Topology", def: 2 / 4, spec: { options: DELAY_TOPOLOGIES } },
  ratio: { label: "L:R Ratio", def: 0.5, spec: { options: DELAY_RATIOS } },
  swing: { label: "Groove", def: 0.5, bipolar: true, spec: { unit: "%", min: -50, max: 50 } },
  feel: { label: "Feel", def: 0.5, bipolar: true, spec: { unit: "ms", min: -50, max: 50 } },
  accent: { label: "Accent", def: 0.5, bipolar: true, spec: { unit: "%", min: -100, max: 100 } },
  drive: { label: "Drive", def: 0, spec: { unit: "dB", min: 0, max: 24 } },
  satType: { label: "Saturation", def: 0, spec: { options: DELAY_SATURATION } },
  diffusion: { label: "Diffusion", def: 0, spec: { unit: "%", min: 0, max: 100 } },
  loopMod: { label: "Loop Mod", def: 0, spec: { unit: "%", min: 0, max: 100 } },
  modRate: { label: "Mod rate", def: 0.3, spec: { unit: "rate", min: 0.05, max: 8, curve: "log" } },
  diffusionSize: { label: "Diffusion size", def: 0.5, spec: { unit: "%", min: 0, max: 100 } },
  ghost: {
    label: "Ghost shift",
    def: 0.5,
    bipolar: true,
    spec: { unit: "Hz", min: -50, max: 50, decimals: 1 },
  },
  lowCut: { label: "Low Cut", def: 0, spec: { unit: "Hz", min: 20, max: 2000, curve: "log" } },
  highCut: { label: "High Cut", def: 1, spec: { unit: "Hz", min: 1000, max: 20000, curve: "log" } },
  pitch: { label: "Pitch", def: 0.5, bipolar: true, spec: { unit: "st", min: -12, max: 12 } },
  freeze: { label: "Freeze", def: 0 },
  duck: { label: "Duck", def: 0, spec: { unit: "%", min: 0, max: 100 } },
  duckRelease: {
    label: "Release",
    def: 0.4,
    spec: { unit: "ms", min: 20, max: 2000, curve: "log" },
  },
  duckThreshold: { label: "Threshold", def: 0.6, spec: { unit: "dB", min: -60, max: 0 } },
  duckKey: { label: "Duck key", def: 0, spec: { options: ["Input", "Ext SC"] } },
  input: { label: "Input", def: 0.5, bipolar: true, spec: { unit: "dB", min: -24, max: 24 } },
  mix: { label: "Mix", def: 0.3, spec: { unit: "%", min: 0, max: 100 } },
  width: { label: "Width", def: 0.5, spec: { unit: "%", min: 0, max: 200 } },
  output: { label: "Output", def: 0.5, bipolar: true, spec: { unit: "dB", min: -24, max: 24 } },
  // Rhythm Echo (8 taps). Defaults = the native defaults.
  rhythmLength: {
    label: "Length",
    def: 1 / 7,
    spec: {
      options: [
        "1 beat",
        "2 beats",
        "3 beats",
        "4 beats",
        "5 beats",
        "6 beats",
        "7 beats",
        "8 beats",
      ],
    },
  },
  rhythmGrid: {
    label: "Grid",
    def: 3 / 5,
    spec: { options: ["1/4", "1/8", "1/8T", "1/16", "1/16T", "1/32"] },
  },
  rhythmTaps: {
    label: "Taps",
    def: 1 / 7,
    spec: { options: ["1", "2", "3", "4", "5", "6", "7", "8"] },
  },
  rhythmShape: {
    label: "Shape",
    def: 0,
    spec: { options: ["Custom", "Decay", "Reverse", "Swell", "Fade"] },
  },
  rhythmPan: {
    label: "Pan",
    def: 0,
    spec: { options: ["Center", "Alternate", "Sweep L", "Sweep R"] },
  },
  rhythmOne: { label: "The One", def: 1, spec: { unit: "%", min: 0, max: 100 } },
  rhythmPos1: { label: "Tap 1 time", def: 0.375 },
  rhythmLvl1: { label: "Tap 1 level", def: 1 },
  rhythmPos2: { label: "Tap 2 time", def: 0.75 },
  rhythmLvl2: { label: "Tap 2 level", def: 0.7 },
  rhythmPos3: { label: "Tap 3 time", def: 0.125 },
  rhythmLvl3: { label: "Tap 3 level", def: 0.6 },
  rhythmPos4: { label: "Tap 4 time", def: 0.5 },
  rhythmLvl4: { label: "Tap 4 level", def: 0.5 },
  rhythmPos5: { label: "Tap 5 time", def: 0.625 },
  rhythmLvl5: { label: "Tap 5 level", def: 0.45 },
  rhythmPos6: { label: "Tap 6 time", def: 0.875 },
  rhythmLvl6: { label: "Tap 6 level", def: 0.4 },
  rhythmPos7: { label: "Tap 7 time", def: 0.25 },
  rhythmLvl7: { label: "Tap 7 level", def: 0.35 },
  rhythmPos8: { label: "Tap 8 time", def: 0.9375 },
  rhythmLvl8: { label: "Tap 8 level", def: 0.3 },
} as const satisfies Schema;

const SCREEN_H = 300;
// Short key captions for the 5-way topology row (the full names stay in the accessible labels of the options list).
export const DELAY_TOPO_LABELS = ["Single", "Dual", "P-Pong", "Ratio", "Rhythm"] as const;
const TOPO_KEYS = ["single", "dual", "pingpong", "ratio", "rhythm"] as const;
type Topo = (typeof TOPO_KEYS)[number];
const toTopo = (label: string): Topo =>
  (TOPO_KEYS as readonly string[]).includes(optionKey(label))
    ? (optionKey(label) as Topo)
    : "pingpong";

function DelayScreen({ getFrame }: { getFrame?: () => unknown }) {
  const fb = useNum("feedback") / 100;
  const topoLabel = useLabel("topology") || DELAY_TOPOLOGIES[2];
  const topology = toTopo(topoLabel);
  const syncOn = useValue("sync") >= 0.5;
  const hasMs = useHas("timeMs");
  const divText = useText("division");
  const msText = useText("timeMs");
  const synced = syncOn || !hasMs;
  const time = synced ? divText : msText;
  const mode = useLabel("mode") || DELAY_MODES[1];
  const duck = useValue("duck");
  const drive = useText("drive");
  const hasDrive = useHas("drive");
  const diffusion = useValue("diffusion");
  const fbText = useText("feedback");
  const duckKey = useOption("duckKey");
  const hasRhythm = useHas("rhythmPos1");
  if (topology === "rhythm" && hasRhythm) {
    return (
      <RhythmEditor
        right={
          <>
            <Lcd style={{ height: 30, fontSize: 12 }}>{mode.toUpperCase()}</Lcd>
            {duck > 0.01 && <SidechainBadge source={duckKey ? "EXT" : "INPUT"} />}
          </>
        }
      />
    );
  }
  return (
    <>
      <Scope
        renderer={tapsRenderer}
        params={{
          feedback: Math.min(0.95, fb),
          topology: topology === "rhythm" ? "single" : topology,
          taps: 10,
          duck: duck > 0.01,
          diffusion: diffusion > 0.01,
          blobs: true,
          rate: 1.15,
        }}
        width={1288}
        height={SCREEN_H}
        label={
          getFrame
            ? `Echo pattern from the current settings: ${topology}, ${time}, feedback ${fbText}`
            : `Echo pattern: ${topology}, ${time}, feedback ${fbText}`
        }
        getFrame={getFrame as () => undefined}
      />
      <Overlay style={{ left: 26, top: 20, display: "flex", flexDirection: "column", gap: 2 }}>
        <Readout size={44} style={{ lineHeight: "46px" }}>
          {time}
        </Readout>
        <Readout size={13} style={{ opacity: 0.8, letterSpacing: ".12em" }}>
          {topoLabel.toUpperCase()} · FEEDBACK {fbText}
          {synced ? " · SYNC" : ""}
        </Readout>
      </Overlay>
      <Overlay style={{ right: 24, top: 22, display: "flex", gap: 8 }}>
        <Lcd style={{ height: 30, fontSize: 13 }}>MODE · {mode.toUpperCase()}</Lcd>
        {hasDrive && <Lcd style={{ height: 30, fontSize: 13 }}>DRIVE {drive}</Lcd>}
        {duck > 0.01 && <SidechainBadge source={duckKey ? "EXT" : "INPUT"} />}
      </Overlay>
    </>
  );
}

function DelayDeck({ meters }: Pick<PanelProps, "meters">) {
  const syncOn = useValue("sync") >= 0.5;
  const hasMs = useHas("timeMs");
  const synced = syncOn || !hasMs;
  const modeKey = optionKey(useLabel("mode"));
  const pitchLive = modeKey === "pitch";
  const ghostLive = modeKey === "ghost";
  const hasGhost = useHas("ghost");
  // The ratio sets R vs L (Dual, Ratio) or Pong vs Ping (Ping-Pong); Single and Rhythm ignore it.
  const topoKey = optionKey(useLabel("topology"));
  const rhythmOn = topoKey === "rhythm";
  const ratioLive = topoKey !== "single" && !rhythmOn;
  const ducking = useValue("duck") > 0.01;
  return (
    <>
      <PGroup
        title="Character"
        chip={system.source}
        keys={["mode", "age", "wow", "flutter"]}
        style={{ width: 270, boxSizing: "border-box" }}
      >
        <PChoice k="mode" columns={3} keyStyle={{ minHeight: 48, fontSize: 12 }} />
        <PSlider k="age" width={254} />
        <PSlider k="wow" width={254} />
        <PSlider k="flutter" width={254} />
      </PGroup>

      <PGroup
        title="Time"
        chip={system.filter}
        keys={["division", "timeMs", "feedback", "topology"]}
        style={{ width: 340, boxSizing: "border-box" }}
        extra={<PCycle k="ratio" style={{ opacity: ratioLive ? 1 : 0.45 }} />}
      >
        <Row justify="space-around">
          {synced ? (
            <PKnob
              k="division"
              size="xl"
              off={rhythmOn}
              label={rhythmOn ? "Time · beat = 1/4" : undefined}
            />
          ) : (
            <PKnob k="timeMs" size="xl" label={rhythmOn ? "Beat" : undefined} />
          )}
          <PKnob k="feedback" size="xl" />
        </Row>
        <Row justify="space-around">
          <PKnob k="swing" size="m" off={rhythmOn} />
          <PKnob k="feel" size="m" />
          <PKnob k="accent" size="m" />
        </Row>
        <PChoice
          k="topology"
          columns={5}
          labels={DELAY_TOPO_LABELS}
          keyStyle={{ minHeight: 36, fontSize: 11, padding: "0 2px", letterSpacing: 0 }}
        />
        <Grid cols={4}>
          <PToggle k="sync" style={{ minHeight: 36 }} />
          <PAction name="tap" style={{ minHeight: 36 }}>
            Tap
          </PAction>
          <PAction name="halve" style={{ minHeight: 36 }}>
            ÷2
          </PAction>
          <PAction name="double" style={{ minHeight: 36 }}>
            ×2
          </PAction>
        </Grid>
      </PGroup>

      <PGroup
        title="Feedback loop"
        chip={system.colour}
        keys={["drive", "diffusion", "loopMod", "modRate", "lowCut", "highCut", "pitch"]}
        style={{ width: 300, boxSizing: "border-box" }}
        extra={<PCycle k="satType" />}
      >
        <Row justify="space-around">
          <PKnob k="drive" />
          <PKnob k="diffusion" />
          <PKnob k="loopMod" />
          <PKnob k="modRate" />
        </Row>
        <PSlider k="diffusionSize" width={268} />
        <Grid cols={2} gap={10}>
          <PSlider k="lowCut" width={129} />
          <PSlider k="highCut" width={129} />
        </Grid>
        {ghostLive && hasGhost ? (
          <PSlider k="ghost" width={268} />
        ) : (
          <PSlider
            k="pitch"
            width={268}
            off={!pitchLive}
            label={pitchLive ? "Pitch" : "Pitch · Pitch mode"}
          />
        )}
        <Grid cols={2}>
          <PToggle k="freeze" style={{ minHeight: 36 }} />
          <PAction name="clear" style={{ minHeight: 36 }}>
            Clear
          </PAction>
        </Grid>
      </PGroup>

      <PGroup
        title="Duck"
        chip={system.mod}
        keys={["duck", "duckRelease", "duckThreshold", "duckKey"]}
        extra={<Led state={ducking ? "mod" : "off"} />}
        style={{ width: 200, boxSizing: "border-box" }}
      >
        <Row>
          <PKnob k="duck" />
          <PKnob k="duckRelease" size="m" />
        </Row>
        <PChoice k="duckKey" keyStyle={{ minHeight: 36 }} />
        <PSlider k="duckThreshold" width={168} accent="#3cd6dd" />
      </PGroup>

      <PGroup
        title="Output"
        chip={system.out}
        keys={["mix", "width", "output"]}
        style={{ flexGrow: 1, minWidth: 0, alignItems: "center" }}
      >
        <PKnob k="mix" size="xl" />
        <PSlider k="input" width={146} />
        <PSlider k="width" width={146} />
        <PSlider k="output" width={146} />
        <StereoMeter read={meters?.out} />
      </PGroup>
    </>
  );
}

export function DelayPanel({
  store,
  map,
  specs,
  presetName,
  onPresetStep,
  meters,
  getFrame,
}: PanelProps) {
  return (
    <PanelScope schema={delaySchema} specs={specs} store={store} map={map}>
      <PluginFrame
        name="DELAY"
        tag="OVERLAY FX"
        subtitle="Tape · BBD · digital · pitch · reverse · ghost"
        presetName={presetName}
        onPresetStep={onPresetStep}
        screenHeight={SCREEN_H}
        screen={<DelayScreen getFrame={getFrame} />}
      >
        <DelayDeck meters={meters} />
      </PluginFrame>
    </PanelScope>
  );
}
