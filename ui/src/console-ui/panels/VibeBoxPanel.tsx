// GENERATED from console-ui — edit there, then run `npm run sync`.
import { Lcd } from "../Surface";
import { Scope } from "../Scope";
import { crushRenderer, wobbleRenderer } from "../renderers";
import { system } from "../tokens";
import { Grid, PAction, PChoice, PGroup, PKnob, PSlider, PToggle, PanelScope, Readout, Row, useHas, useLabel, useNum, useText, useValue, type PanelProps, type Schema } from "./controls";
import { Overlay, PluginFrame } from "./PluginFrame";

export const VIBE_BITS = ["24", "16", "14", "12", "10", "8", "6", "4", "2"] as const;
export const VIBE_RPM = ["33⅓", "45", "78"] as const;
export const VIBE_CONSOLES = ["British", "American", "Tube"] as const;
// Generic descriptions only — no hardware trademarks in a shipping product.
export const VIBE_MACHINES = ["12-bit drum sampler", "26k swing sampler", "Studio sampler", "Cassette 4-track", "Half-inch tape"] as const;

export const vibeboxSchema = {
  magnitude: { label: "Magnitude", def: 0.5, spec: { unit: "%", min: 0, max: 100 } },
  mix: { label: "Mix", def: 1, spec: { unit: "%", min: 0, max: 100 } },
  output: { label: "Output", def: 0.5, bipolar: true, spec: { unit: "dB", min: -24, max: 24 } },
  space: { label: "Space", def: 0, spec: { unit: "%", min: 0, max: 100 } },
  decOn: { label: "Decimate", def: 1 },
  bits: { label: "Bits", def: 3 / 8, spec: { options: VIBE_BITS } },
  rate: { label: "Rate", def: 1, spec: { unit: "Hz", min: 400, max: 48000, curve: "log" } },
  jitter: { label: "Jitter", def: 0, spec: { unit: "%", min: 0, max: 100 } },
  aaPre: { label: "AA Pre", def: 1 },
  aaPost: { label: "AA Post", def: 0 },
  dither: { label: "Dither", def: 0 },
  quantize: { label: "Quantize", def: 0, spec: { options: ["Precise", "Approx"] } },
  postFilter: { label: "Post filter", def: 1, spec: { unit: "Hz", min: 1000, max: 20000, curve: "log" } },
  vinylOn: { label: "Vinyl", def: 1 },
  rpm: { label: "RPM", def: 0, spec: { options: VIBE_RPM } },
  crackle: { label: "Crackle", def: 0.3, spec: { unit: "%", min: 0, max: 100 } },
  wear: { label: "Wear", def: 0.3, spec: { unit: "%", min: 0, max: 100 } },
  wow: { label: "Wow", def: 0.2, spec: { unit: "%", min: 0, max: 100 } },
  flutter: { label: "Flutter", def: 0.1, spec: { unit: "%", min: 0, max: 100 } },
  dust: { label: "Dust", def: 0.2, spec: { unit: "%", min: 0, max: 100 } },
  rumble: { label: "Rumble", def: 0.1, spec: { unit: "%", min: 0, max: 100 } },
  pressing: { label: "Pressing age", def: 0.5, spec: { options: ["New", "70s", "60s", "50s", "Shellac"] } },
  consoleOn: { label: "Console", def: 1 },
  consoleModel: { label: "Console model", def: 0, spec: { options: VIBE_CONSOLES } },
  drive: { label: "Drive", def: 0.3, spec: { unit: "dB", min: 0, max: 24 } },
  crosstalk: { label: "Crosstalk", def: 0.1, spec: { unit: "%", min: 0, max: 100 } },
  consoleNoise: { label: "Noise", def: 0.1, spec: { unit: "%", min: 0, max: 100 } },
  tolerance: { label: "Component tolerance", def: 0.2, spec: { unit: "%", min: 0, max: 100 } },
  deskPos: { label: "Desk position", def: 0, spec: { options: ["Channel", "Mix Bus"] } },
  machineOn: { label: "Machine", def: 1 },
  machine: { label: "Machine", def: 0, spec: { options: VIBE_MACHINES } },
  machineAmount: { label: "Amount", def: 0.5, spec: { unit: "%", min: 0, max: 100 } },
  machineAge: { label: "Age", def: 0.3, spec: { unit: "%", min: 0, max: 100 } },
  pitchDown: { label: "Pitch-down sampling", def: 0, spec: { unit: "st", min: 0, max: 12 } },
} as const satisfies Schema;

export interface VibeBoxPanelProps extends PanelProps { noiseSampleName?: string | null }

function Turntable({ spinning, rpm, label }: { spinning: boolean; rpm: number; label: string }) {
  const period = 60 / rpm;
  return (
    <div style={{ position: "relative", width: 330, flexShrink: 0, borderRadius: 8, overflow: "hidden", background: "radial-gradient(ellipse at 40% 30%, #1d1307, #0b0703 80%)" }} role="img" aria-label={`Turntable ${spinning ? `spinning at ${label}` : "stopped"}`}>
      <div style={{ position: "absolute", left: 22, top: 40, width: 260, height: 260, borderRadius: "50%", background: "#0a0a0a", boxShadow: "0 14px 30px rgba(0,0,0,.8), 0 0 0 6px #1a1a1c, 0 0 0 7px #000" }} />
      <div style={{ position: "absolute", left: 30, top: 48, width: 244, height: 244, borderRadius: "50%", background: "repeating-radial-gradient(circle, #121212 0 1.5px, #070707 1.5px 3px)", animation: spinning ? `cs-spin ${period}s linear infinite` : "none" }}>
        <div style={{ position: "absolute", left: 82, top: 82, width: 80, height: 80, borderRadius: "50%", background: "radial-gradient(circle, #0a0a0a 0 4px, #ff8a00 5px, #e67300 70%, #b35900)", display: "flex", flexDirection: "column", alignItems: "center", justifyContent: "center", gap: 22 }}>
          <span style={{ fontWeight: 800, fontSize: 11, letterSpacing: ".1em", color: "#1a0f02" }}>VIBEBOX</span>
          <span style={{ fontFamily: "var(--cs-font-mono)", color: "#1a0f02" }}>{label}</span>
        </div>
      </div>
      <div style={{ position: "absolute", left: 30, top: 48, width: 244, height: 244, borderRadius: "50%", pointerEvents: "none", background: "conic-gradient(from 20deg, transparent 0 30deg, rgba(255,255,255,.1) 45deg, transparent 60deg 200deg, rgba(255,255,255,.07) 215deg, transparent 230deg)" }} />
      <svg style={{ position: "absolute", left: 0, top: 0 }} width="330" height="336" viewBox="0 0 330 336" aria-hidden="true">
        <circle cx="300" cy="56" r="16" fill="#2a2a2e" stroke="#000" /><circle cx="300" cy="56" r="6" fill="#8d8d93" />
        <path d={spinning ? "M300 56 L292 190 L248 238" : "M300 56 L306 190 L300 250"} fill="none" stroke="#c9c9ce" strokeWidth="5" strokeLinecap="round" />
      </svg>
    </div>
  );
}

function VibeScreen({ noiseSampleName }: { noiseSampleName?: string | null }) {
  const vinylVal = useValue("vinylOn");
  const hasVinylToggle = useHasOne("vinylOn");
  const vinylOn = vinylVal >= 0.5 || !hasVinylToggle;
  const rpmLabel = useLabel("rpm") || VIBE_RPM[0];
  const bitsLabel = useLabel("bits") || VIBE_BITS[3];
  const jitter = useValue("jitter");
  const wow = useValue("wow");
  const crackle = useValue("crackle");
  const dust = useValue("dust");
  const decOn = useValue("decOn") >= 0.5;
  const bitsText = bitsLabel;
  const rateText = useText("rate");
  const wearText = useText("wear");
  const consoleName = useLabel("consoleModel") || VIBE_CONSOLES[0];
  const machineName = useLabel("machine") || VIBE_MACHINES[0];
  const consoleVal = useValue("consoleOn");
  const hasConsole = useHasOne("consoleModel");
  const machineVal = useValue("machineOn");
  const hasMachine = useHasOne("machine");
  const consoleOn = consoleVal >= 0.5 && hasConsole;
  const machineOn = machineVal >= 0.5 && hasMachine;
  const hasVinyl = useHasOne("rpm", "crackle");
  const bits = Number.parseFloat(bitsText) || 12;
  const rateHz = useNum("rate");
  const rpm = Number.parseFloat(rpmLabel) || 33.333;
  const chain = [consoleOn && `${consoleName.toUpperCase()} DESK`, machineOn && machineName.toUpperCase()].filter(Boolean).join(" → ") || "CLEAN";
  return (
    <div className="cs-screen" style={{ height: 344, boxSizing: "border-box", display: "flex", gap: 4 }}>
      {hasVinyl && (
        <div style={{ position: "relative", flexShrink: 0, display: "flex" }}>
          <Turntable spinning={vinylOn} rpm={rpm} label={`${rpmLabel} RPM`} />
          {noiseSampleName !== undefined && (
            <Overlay interactive style={{ left: 12, right: 12, bottom: 12, display: "flex", gap: 8, alignItems: "center" }}>
              <Lcd style={{ flexGrow: 1, minWidth: 0, height: 36, fontSize: 12, overflow: "hidden", whiteSpace: "nowrap" }}>{noiseSampleName ?? "Built-in surface"}</Lcd>
              <PAction name="loadNoise">Load</PAction>
            </Overlay>
          )}
        </div>
      )}
      <div style={{ position: "relative", flexShrink: 0 }}>
        <Scope renderer={crushRenderer} params={{ levels: decOn ? Math.max(1, Math.min(10, bits / 2)) : 40, holdPx: decOn ? Math.max(2, 48000 / rateHz * 1.5) : 2, jitter }} width={hasVinyl ? 520 : 760} height={336} label={`Decimation ${bitsText} bit at ${rateText}`} />
        <Overlay style={{ left: 18, top: 14, display: "flex", flexDirection: "column" }}>
          <Readout size={38} style={{ lineHeight: "40px" }}>{decOn ? <>{bitsText}<span style={{ fontSize: 18 }}>-BIT</span> · {rateText}</> : "DECIMATE OFF"}</Readout>
          <Readout size={12} style={{ opacity: 0.75, letterSpacing: ".12em" }}>JITTER {Math.round(jitter * 100)}%</Readout>
        </Overlay>
      </div>
      <div style={{ position: "relative", flexGrow: 1, minWidth: 0 }}>
        <Scope renderer={wobbleRenderer} params={{ wow: vinylOn ? wow * 1.4 : 0.05, noise: vinylOn ? Math.min(1, crackle + dust) : 0.05 }} width={hasVinyl ? 422 : 512} height={336} label="Print trace" />
        <Overlay style={{ left: 18, top: 14, display: "flex", flexDirection: "column", gap: 2 }}>
          <Readout size={12} style={{ letterSpacing: ".12em" }}>PRINT CHAIN</Readout>
          <Readout size={17}>{chain}</Readout>
          {hasVinyl && <Readout size={12} style={{ opacity: 0.75, letterSpacing: ".1em" }}>{rpmLabel} RPM · WEAR {wearText}</Readout>}
        </Overlay>
      </div>
    </div>
  );
}

function useHasOne(...keys: string[]) { return useHas(...keys); }

function EngineGroup({ title, chip, onKey, keys, width, children }: { title: string; chip: string; onKey: string; keys: string[]; width?: number; children: React.ReactNode }) {
  return (
    <PGroup title={title} chip={chip} keys={keys} enable={onKey} style={width ? { width, boxSizing: "border-box" } : { flexGrow: 1, minWidth: 0 }}>
      <div style={{ display: "flex", flexDirection: "column", gap: 10 }}>{children}</div>
    </PGroup>
  );
}

export function VibeBoxPanel({ store, map, specs, presetName, onPresetStep, noiseSampleName }: VibeBoxPanelProps) {
  return (
    <PanelScope schema={vibeboxSchema} specs={specs} store={store} map={map}>
      <PluginFrame name="VIBEBOX" tag="OVERLAY FX · LO-FI" subtitle="Decimate · vinyl · console · classic machines" presetName={presetName} onPresetStep={onPresetStep}
        headerRight={<PAction name="dice" style={{ minHeight: 40 }}>Dice</PAction>}
        screen={<VibeScreen noiseSampleName={noiseSampleName} />}>
        <VibeDeck />
      </PluginFrame>
    </PanelScope>
  );
}

function VibeDeck() {
  const mag = useValue("magnitude");
  const hasMag = useHas("magnitude");
  const ring = hasMag ? { modDepth: 0.05 + mag * 0.15, modSource: "Magnitude" } : {};
  return (
    <>
      <PGroup title="Magnitude" chip={system.mod} keys={["magnitude", "mix", "output"]} style={{ width: 210, boxSizing: "border-box", alignItems: "center" }}>
        <PKnob k="magnitude" size="xxl" showValue />
        <Row gap={4}><PKnob k="mix" /><PKnob k="output" /></Row>
        <PSlider k="space" width={194} />
      </PGroup>

      <EngineGroup title="Decimate" chip={system.filter} onKey="decOn" keys={["bits", "rate", "jitter"]} width={280}>
        <Row><PKnob k="bits" {...ring} /><PKnob k="rate" /><PKnob k="jitter" /></Row>
        <Grid cols={3}><PToggle k="aaPre" style={{ fontSize: 11 }} /><PToggle k="aaPost" style={{ fontSize: 11 }} /><PToggle k="dither" style={{ fontSize: 11 }} /></Grid>
        <PChoice k="quantize"  />
        <PSlider k="postFilter" width={248} />
      </EngineGroup>

      <EngineGroup title="Vinyl" chip={system.source} onKey="vinylOn" keys={["rpm", "crackle", "wear", "wow"]} width={260}>
        <PChoice k="rpm"  />
        <Row><PKnob k="crackle" {...ring} /><PKnob k="wear" /><PKnob k="wow" /></Row>
        <Grid cols={2} gap={10}><PSlider k="dust" width={109} /><PSlider k="rumble" width={109} /></Grid>
        <PSlider k="pressing" width={228} />
      </EngineGroup>

      <EngineGroup title="Console" chip={system.colour} onKey="consoleOn" keys={["consoleModel", "drive", "crosstalk"]} width={280}>
        <PChoice k="consoleModel" keyStyle={{ letterSpacing: ".04em", padding: "0 6px" }} />
        <Row><PKnob k="drive" {...ring} /><PKnob k="crosstalk" /><PKnob k="consoleNoise" /></Row>
        <PSlider k="tolerance" width={228} />
        <PChoice k="deskPos" keyStyle={{ fontSize: 11 }} />
      </EngineGroup>

      <EngineGroup title="Machine" chip={system.env} onKey="machineOn" keys={["machine", "machineAmount"]}>
        <Row gap={10} align="flex-start">
          <PChoice k="machine" columns={1} style={{ flexGrow: 1 }} keyStyle={{ textAlign: "left", padding: "0 10px", letterSpacing: ".04em" }} />
          <div style={{ display: "flex", flexDirection: "column", alignItems: "center", gap: 6 }}><PKnob k="machineAmount" {...ring} /><PKnob k="machineAge" size="m" /></div>
        </Row>
        <PSlider k="pitchDown" width={246} />
      </EngineGroup>
    </>
  );
}
