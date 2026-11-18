import { useEffect, useMemo, useState } from "react";
import {
  ChannelStripPanel,
  MasterPanel,
  channelStripSchema,
  masterSchema,
  createStateStore,
  toNorm,
  toValue,
  type ParamMap,
  type ParamSpec,
} from "../console-ui";
import type { ChannelParams, MasterParams } from "../types";

/**
 * Live faceplates for the baked MixRack.
 *
 * The console-ui panels were written for the standalone Overlay plugins; their
 * keys already match the MixRack parameter surface for bands 1-4, the
 * compressor, saturator and filters. This adapter maps those keys to the
 * host's `setChannelParam` / `setMasterParam` routes, so the rack is edited
 * through exactly the DSP that renders the mix.
 */

const HZ = (min: number, max: number): ParamSpec => ({ min, max, curve: "log", unit: "Hz" });
const DB = (min: number, max: number): ParamSpec => ({ min, max, unit: "dB" });
const PCT = (max = 100): ParamSpec => ({ min: 0, max, unit: "%" });
const RATIO: ParamSpec = { min: 1, max: 20, curve: "log", unit: "ratio" };
const MS = (min: number, max: number): ParamSpec => ({ min, max, curve: "log", unit: "ms" });

const STRIP_SHAPES = [
  "Bell",
  "Low Shelf",
  "High Shelf",
  "Low Cut",
  "High Cut",
  "Notch",
  "Band Pass",
  "All Pass",
];
const COMP_TYPES = ["VCA", "FET", "Opto", "VariMu", "Bus", "Punch", "Transparent", "Limiter"];
const SAT_MODES = ["Tape", "Tube", "Diode", "Soft", "Warm", "Fold", "Fuzz", "Crunch"];

const stripSpecs: Record<string, ParamSpec> = {
  trim: DB(-24, 24),
  hpfFreq: HZ(10, 1000),
  lpfFreq: HZ(1000, 20000),
  compThresh: DB(-60, 0),
  compRatio: RATIO,
  compAttack: MS(0.05, 200),
  compRelease: MS(10, 2000),
  compKnee: DB(0, 24),
  compMakeup: DB(-12, 24),
  compMix: PCT(),
  compDrive: DB(-24, 24),
  compOutput: DB(-24, 24),
  compColor: PCT(),
  compFeedback: PCT(45),
  compLookahead: { min: 0, max: 20, unit: "ms" },
  compHold: { min: 0, max: 500, unit: "ms" },
  compRange: { min: 0, max: 60, unit: "dB" },
  compAmount: PCT(200),
  scHpf: HZ(1, 1000),
  scEqFreq: HZ(20, 20000),
  scEqGain: DB(-18, 18),
  scEqQ: { min: 0.1, max: 8, curve: "log", decimals: 2 },
  satDrive: DB(0, 36),
  satMix: PCT(),
  satBias: { min: -1, max: 1, decimals: 2 },
  satTone: { min: -1, max: 1, decimals: 2 },
  satToneBass: { min: -1, max: 1, decimals: 2 },
  satToneMid: { min: -1, max: 1, decimals: 2 },
  satToneTreble: { min: -1, max: 1, decimals: 2 },
  satTonePresence: { min: -1, max: 1, decimals: 2 },
  satDynamics: { min: -1, max: 1, decimals: 2 },
  satFeedback: PCT(),
  satOutput: DB(-24, 24),
  width: PCT(200),
  output: DB(-60, 24),
  dynAttack: MS(0.1, 200),
  dynRelease: MS(1, 2000),
  ...Object.fromEntries(
    [1, 2, 3, 4].flatMap((n) => [
      [`band${n}Freq`, HZ(20, 20000)],
      [`band${n}Gain`, DB(-26, 26)],
      [`band${n}Q`, { min: 0.05, max: 24, curve: "log", decimals: 2 }],
      [`band${n}DynThresh`, DB(-60, 0)],
      [`band${n}DynRange`, DB(-24, 24)],
    ]),
  ),
};

const stripOverrides = {
  ...Object.fromEntries(Object.entries(stripSpecs).map(([key, spec]) => [key, { spec }])),
  band1Type: { spec: { options: STRIP_SHAPES } },
  band2Type: { spec: { options: STRIP_SHAPES } },
  band3Type: { spec: { options: STRIP_SHAPES } },
  band4Type: { spec: { options: STRIP_SHAPES } },
  compType: { spec: { options: COMP_TYPES } },
  satMode: { spec: { options: SAT_MODES } },
};

const stripParamFor: Record<string, string> = {
  eqOn: "eqOn",
  hpfOn: "hpfOn",
  lpfOn: "lpfOn",
  hpfFreq: "hpfFreq",
  lpfFreq: "lpfFreq",
  hpfSlope: "hpfSlope",
  lpfSlope: "lpfSlope",
  dynAttack: "eqDynAttack",
  dynRelease: "eqDynRelease",
  trim: "inputTrim",
  phase: "phase",
  compOn: "compOn",
  compType: "compType",
  compThresh: "compThresh",
  compRatio: "compRatio",
  compAttack: "compAttack",
  compRelease: "compRelease",
  compKnee: "compKnee",
  compMakeup: "compMakeup",
  compMix: "compMix",
  compDrive: "compDrive",
  compOutput: "compOutput",
  compColor: "compColor",
  compFeedback: "compFeedback",
  compLookahead: "compLookahead",
  compHold: "compHold",
  compRange: "compRange",
  compAmount: "compAmount",
  compAutoRelease: "compAutoRelease",
  compAutoGain: "compAutoGain",
  scHpf: "scHpf",
  scEqFreq: "scEqFreq",
  scEqGain: "scEqGain",
  scEqQ: "scEqQ",
  satOn: "satOn",
  satMode: "satMode",
  satDrive: "satDrive",
  satMix: "satMix",
  satBias: "satBias",
  satTone: "satTone",
  satToneBass: "satToneBass",
  satToneMid: "satToneMid",
  satToneTreble: "satToneTreble",
  satTonePresence: "satTonePresence",
  satDynamics: "satDynamics",
  satFeedback: "satFeedback",
  satOversample: "satOversample",
  satOutput: "satOutput",
  width: "width",
  mono: "mono",
  output: "outputDb",
  template: "",
  scEq: "",
  scExternal: "",
};

for (let n = 1; n <= 4; n++) {
  for (const suffix of [
    "On",
    "Type",
    "Freq",
    "Gain",
    "Q",
    "Slope",
    "Dyn",
    "DynThresh",
    "DynRange",
  ]) {
    stripParamFor[`band${n}${suffix}`] = `band${n}${suffix}`;
  }
}

const needsPercentConversion = new Set([
  "compMix",
  "satMix",
  "compAmount",
  "satFeedback",
  "compColor",
  "compFeedback",
]);
const needsDoubleConversion = new Set(["width"]);

function normalizeStripValue(key: string, raw: number | boolean | undefined): number | undefined {
  if (typeof raw === "boolean") return raw ? 1 : 0;
  if (typeof raw !== "number") return undefined;
  const spec = stripSpecs[key] ?? channelStripSchema[key]?.spec;
  if (!spec) return undefined;

  if (needsPercentConversion.has(key)) return toNorm(raw * 100, spec);
  if (needsDoubleConversion.has(key)) return toNorm(raw * 100, spec);
  return toNorm(raw, spec);
}

function denormalizeStripValue(key: string, norm: number): number {
  const spec = stripSpecs[key] ?? channelStripSchema[key]?.spec ?? { min: 0, max: 1 };
  const value = toValue(norm, spec);
  if (needsPercentConversion.has(key)) return value / 100;
  if (needsDoubleConversion.has(key)) return value / 100;
  return value;
}

function makeStripStore(channel: ChannelParams, onParam: (id: string, value: number) => void) {
  const map: ParamMap = Object.fromEntries(
    Object.keys(channelStripSchema).map((key) => [key, key]),
  );

  const store = createStateStore<ChannelParams>({
    read(source, id) {
      const param = stripParamFor[id];
      if (!param) return undefined;
      return normalizeStripValue(
        id,
        (source as unknown as Record<string, number | boolean | undefined>)[param],
      );
    },
    write(id, norm) {
      const param = stripParamFor[id];
      if (!param) return;
      onParam(param, denormalizeStripValue(id, norm));
    },
  });

  store.update(channel);
  return { store, map };
}

export function ChannelRackPanel({
  channel,
  onParam,
  onClose,
}: {
  channel?: ChannelParams;
  onParam: (id: string, value: number) => void;
  onClose: () => void;
}) {
  const [scale, setScale] = useState(0.72);
  const { store, map } = useMemo(
    () => makeStripStore(channel ?? ({} as ChannelParams), onParam),
    [channel, onParam],
  );

  useEffect(() => {
    if (channel) store.update(channel);
  }, [channel, store]);

  useEffect(() => {
    const key = (event: KeyboardEvent) => {
      if (event.key === "Escape") onClose();
    };
    window.addEventListener("keydown", key);
    return () => window.removeEventListener("keydown", key);
  }, [onClose]);

  return (
    <div
      className="ws-rack-overlay"
      role="dialog"
      aria-modal="true"
      aria-label="Overlay Channel Strip"
      onPointerDown={(event) => {
        if (event.target === event.currentTarget) onClose();
      }}
    >
      <div className="ws-rack-bar">
        <button
          type="button"
          className="ws-mini-btn"
          onClick={() => setScale((s) => Math.max(0.45, s - 0.05))}
        >
          −
        </button>
        <button
          type="button"
          className="ws-mini-btn"
          onClick={() => setScale((s) => Math.min(1.1, s + 0.05))}
        >
          +
        </button>
        <button type="button" className="ws-mini-btn" onClick={onClose}>
          CLOSE
        </button>
      </div>
      <div className="ws-rack-scroll">
        <div style={{ width: 1320 * scale, height: 780 * scale, position: "relative" }}>
          <div
            style={{
              width: 1320,
              height: 780,
              transform: `scale(${scale})`,
              transformOrigin: "top left",
            }}
          >
            <ChannelStripPanel
              store={store}
              map={map}
              specs={stripOverrides}
              presetName="LIVE · MIXRACK"
            />
          </div>
        </div>
      </div>
    </div>
  );
}

// ---------------------------------------------------------------- master

const MASTER_TARGETS = [-14, -16, -23, -24, -11, -15];

const masterSpecs: Record<string, ParamSpec> = {
  outputTrim: DB(-12, 12),
  maxThresh: DB(-24, 0),
  maxCeiling: { min: -6, max: 0, unit: "dBTP" },
  maxRelease: MS(10, 1000),
  maxTransient: PCT(),
  maxLink: PCT(),
  dynKnee: DB(0, 24),
  dynMix: PCT(),
  dynX1: HZ(40, 600),
  dynX2: HZ(300, 6000),
  dynX3: HZ(2000, 16000),
  excX1: HZ(40, 600),
  excX2: HZ(300, 6000),
  excX3: HZ(2000, 16000),
  imgX1: HZ(40, 600),
  imgX2: HZ(300, 6000),
  imgX3: HZ(2000, 16000),
  ...Object.fromEntries(
    [1, 2, 3, 4].flatMap((n) => [
      [`dyn${n}Thresh`, DB(-60, 0)],
      [`dyn${n}Ratio`, { min: 1, max: 20, curve: "log", unit: "ratio" }],
      [`dyn${n}Atk`, MS(0.1, 200)],
      [`dyn${n}Rel`, MS(10, 2000)],
      [`dyn${n}Gain`, DB(-12, 12)],
      [`exc${n}Drive`, DB(0, 24)],
      [`exc${n}Mix`, PCT()],
      [`exc${n}Dyn`, { min: -1, max: 1, decimals: 2 }],
      [`exc${n}Feedback`, PCT()],
      [`exc${n}ToneBass`, { min: -1, max: 1, decimals: 2 }],
      [`exc${n}ToneMid`, { min: -1, max: 1, decimals: 2 }],
      [`exc${n}ToneTreble`, { min: -1, max: 1, decimals: 2 }],
      [`exc${n}TonePresence`, { min: -1, max: 1, decimals: 2 }],
      [`exc${n}Level`, DB(-24, 24)],
      [`img${n}Width`, PCT(200)],
    ]),
  ),
  ...Object.fromEntries(
    [1, 2, 3, 4, 5, 6, 7, 8].flatMap((n) => [
      [`eq${n}Freq`, HZ(20, 20000)],
      [`eq${n}Gain`, DB(-18, 18)],
      [`eq${n}Q`, { min: 0.1, max: 10, curve: "log", decimals: 2 }],
    ]),
  ),
  ...Object.fromEntries(
    [1, 2, 3, 4].flatMap((n) => [
      [`deq${n}Freq`, HZ(20, 20000)],
      [`deq${n}Q`, { min: 0.3, max: 6, curve: "log", decimals: 2 }],
      [`deq${n}Thresh`, DB(-60, 0)],
      [`deq${n}Range`, DB(-12, 12)],
      [`deq${n}Atk`, MS(0.5, 200)],
      [`deq${n}Rel`, MS(10, 2000)],
    ]),
  ),
};

const masterOverrides = {
  ...Object.fromEntries(Object.entries(masterSpecs).map(([key, spec]) => [key, { spec }])),
  ...Object.fromEntries([1, 2, 3, 4].map((n) => [`exc${n}Mode`, { spec: { options: SAT_MODES } }])),
};

const masterParamFor: Record<string, string> = {
  eqOn: "eqOn",
  deqOn: "deqOn",
  dynOn: "dynOn",
  excOn: "excOn",
  imgOn: "imgOn",
  maxOn: "maxOn",
  compare: "compare",
  outputTrim: "trimDb",
  maxThresh: "maxThresh",
  maxCeiling: "ceiling",
  maxRelease: "maxRelease",
  maxMode: "maxMode",
  maxOversample: "maxOversample",
  maxTransient: "maxTransient",
  maxLink: "maxLink",
  dynBands: "dynBands",
  dynX1: "dynX1",
  dynX2: "dynX2",
  dynX3: "dynX3",
  dynKnee: "dynKnee",
  dynDetect: "dynDetect",
  dynMix: "dynMix",
  excBands: "excBands",
  excX1: "excX1",
  excX2: "excX2",
  excX3: "excX3",
  imgBands: "imgBands",
  imgX1: "imgX1",
  imgX2: "imgX2",
  imgX3: "imgX3",
  imgMono: "imgMono",
  target: "targetLufs",
};

for (let n = 1; n <= 8; n++) {
  for (const suffix of ["On", "Type", "Freq", "Gain", "Q", "Slope", "Ch"]) {
    masterParamFor[`eq${n}${suffix}`] = `band${n}${suffix}`;
  }
}

for (let n = 1; n <= 4; n++) {
  for (const suffix of ["On", "Type", "Freq", "Q", "Thresh", "Range", "Atk", "Rel", "Ch"]) {
    masterParamFor[`deq${n}${suffix}`] = `deq${n}${suffix}`;
  }
  for (const suffix of ["On", "Thresh", "Ratio", "Atk", "Rel", "Gain"]) {
    masterParamFor[`dyn${n}${suffix}`] = `dyn${n}${suffix}`;
  }
  for (const suffix of [
    "Drive",
    "Mix",
    "Mode",
    "Dyn",
    "Feedback",
    "ToneBass",
    "ToneMid",
    "ToneTreble",
    "TonePresence",
    "Level",
  ]) {
    masterParamFor[`exc${n}${suffix}`] = `exc${n}${suffix}`;
  }
  masterParamFor[`img${n}Width`] = `img${n}Width`;
}

const masterPercentKeys = new Set(["dynMix", "maxTransient", "maxLink"]);
const masterDoubleKeys = new Set<string>();
for (let n = 1; n <= 4; n++) {
  masterPercentKeys.add(`exc${n}Mix`);
  masterPercentKeys.add(`exc${n}Feedback`);
  masterDoubleKeys.add(`img${n}Width`);
}

function normalizeMasterValue(key: string, raw: number | boolean | undefined): number | undefined {
  if (typeof raw === "boolean") return raw ? 1 : 0;
  if (typeof raw !== "number") return undefined;

  if (key === "target") {
    const index = MASTER_TARGETS.findIndex((value) => Math.abs(value - raw) < 0.5);
    return index >= 0 ? index / (MASTER_TARGETS.length - 1) : 0;
  }
  if (key === "maxOversample") return raw / 3;
  if (key === "maxMode") return raw / 2;
  if (key === "dynBands" || key === "excBands" || key === "imgBands") return raw / 3;

  const spec = masterSpecs[key] ?? masterSchema[key]?.spec;
  if (!spec) return undefined;
  if (masterPercentKeys.has(key)) return toNorm(raw * 100, spec);
  if (masterDoubleKeys.has(key)) return toNorm(raw * 100, spec);
  return toNorm(raw, spec);
}

function denormalizeMasterValue(key: string, norm: number): number {
  if (key === "target") {
    const index = Math.round(norm * (MASTER_TARGETS.length - 1));
    return MASTER_TARGETS[Math.max(0, Math.min(MASTER_TARGETS.length - 1, index))];
  }
  if (key === "maxOversample") return Math.round(norm * 3);
  if (key === "maxMode") return Math.round(norm * 2);
  if (key === "dynBands" || key === "excBands" || key === "imgBands") return Math.round(norm * 3);

  const spec = masterSpecs[key] ?? masterSchema[key]?.spec ?? { min: 0, max: 1 };
  const value = toValue(norm, spec);
  if (masterPercentKeys.has(key)) return value / 100;
  if (masterDoubleKeys.has(key)) return value / 100;
  return value;
}

function makeMasterStore(master: MasterParams, onParam: (id: string, value: number) => void) {
  const map: ParamMap = Object.fromEntries(Object.keys(masterSchema).map((key) => [key, key]));

  const store = createStateStore<MasterParams>({
    read(source, id) {
      const param = masterParamFor[id];
      if (!param) return undefined;
      return normalizeMasterValue(
        id,
        (source as unknown as Record<string, number | boolean | undefined>)[param],
      );
    },
    write(id, norm) {
      const param = masterParamFor[id];
      if (!param) return;
      onParam(param, denormalizeMasterValue(id, norm));
    },
  });

  store.update(master);
  return { store, map };
}

export function MasterRackPanel({
  master,
  onParam,
  onClose,
}: {
  master?: MasterParams;
  onParam: (id: string, value: number) => void;
  onClose: () => void;
}) {
  const [scale, setScale] = useState(0.68);
  const { store, map } = useMemo(
    () => makeMasterStore(master ?? ({} as MasterParams), onParam),
    [master, onParam],
  );

  useEffect(() => {
    if (master) store.update(master);
  }, [master, store]);

  useEffect(() => {
    const key = (event: KeyboardEvent) => {
      if (event.key === "Escape") onClose();
    };
    window.addEventListener("keydown", key);
    return () => window.removeEventListener("keydown", key);
  }, [onClose]);

  return (
    <div
      className="ws-rack-overlay"
      role="dialog"
      aria-modal="true"
      aria-label="Overlay Master"
      onPointerDown={(event) => {
        if (event.target === event.currentTarget) onClose();
      }}
    >
      <div className="ws-rack-bar">
        <button
          type="button"
          className="ws-mini-btn"
          onClick={() => setScale((s) => Math.max(0.45, s - 0.05))}
        >
          −
        </button>
        <button
          type="button"
          className="ws-mini-btn"
          onClick={() => setScale((s) => Math.min(1.1, s + 0.05))}
        >
          +
        </button>
        <button type="button" className="ws-mini-btn" onClick={onClose}>
          CLOSE
        </button>
      </div>
      <div className="ws-rack-scroll">
        <div style={{ width: 1320 * scale, height: 780 * scale, position: "relative" }}>
          <div
            style={{
              width: 1320,
              height: 780,
              transform: `scale(${scale})`,
              transformOrigin: "top left",
            }}
          >
            <MasterPanel
              store={store}
              map={map}
              specs={masterOverrides}
              presetName="LIVE · MASTER"
            />
          </div>
        </div>
      </div>
    </div>
  );
}
