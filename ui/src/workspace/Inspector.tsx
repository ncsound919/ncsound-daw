import { useEffect, useRef, useState } from "react";
import type { ChannelParams, PitchState, TransportState, TrackInfo } from "../types";
import { PitchKnobs } from "../components/PitchKnobs";
import { Knob } from "../console-ui/Knob";
import { toNorm, toValue, type ParamSpec } from "../console-ui/param";
import { INSERT_BANKS, dbLabel, fitScale, meterPct, tintForTrack } from "./format";
import { PANEL_H, PANEL_W } from "../console-ui/panels";
import { OverlayFace, moduleById, moduleForInsert, previewReadout } from "./overlayFx";

const GAIN_SPEC: ParamSpec = { min: -60, max: 6, decimals: 1, unit: "dB" };
const PAN_SPEC: ParamSpec = { min: -1, max: 1, decimals: 2 };
const SEND_SPEC: ParamSpec = { min: 0, max: 100, decimals: 0, unit: "%" };
const HPF_SPEC: ParamSpec = { min: 20, max: 500, decimals: 0, unit: "Hz" };
const THRESH_SPEC: ParamSpec = { min: -60, max: 0, decimals: 1, unit: "dB" };
const OUTPUT_SPEC: ParamSpec = { min: -24, max: 12, decimals: 1, unit: "dB" };

export interface InspectorProps {
  track?: TrackInfo;
  transport?: TransportState;
  onGain: (track: number, db: number) => void;
  onPan: (track: number, pan: number) => void;
  onMute: (track: number, muted: boolean) => void;
  onSolo: (track: number, soloed: boolean) => void;
  onArm: (track: number, armed: boolean) => void;
  onSendA: (track: number, amount: number) => void;
  onSendB: (track: number, amount: number) => void;
  /** Only rendered as a button when the host has a load route; static otherwise. */
  onInsert?: (slot: number) => void;
  /** Baked MixRack parameters for this track, when the host publishes them. */
  channel?: ChannelParams;
  onChannelParam?: (id: string, value: number) => void;
  /** Arms the host's MIDI learn for one strip parameter id. */
  onMidiLearn?: (id: string) => void;
  /** Arrange rail shows the channel controls only; the tab shows the editor too. */
  compact?: boolean;
  /** Track pitch lives in the output section's open space. */
  pitch?: PitchState;
  onPitch?: (patch: Partial<PitchState>) => void;
  instrumentPitch?: PitchState;
  onInstrumentPitch?: (patch: Partial<PitchState>) => void;
  /** The instrument follows the track pitch stack while this is on. */
  pitchSync?: boolean;
  onPitchSync?: (synced: boolean) => void;
}

function focusedModuleKey(slot: number | null): string {
  return slot === null ? "none" : String(slot);
}

function panLabel(pan: number): string {
  if (Math.abs(pan) < 0.02) return "C";
  return `${pan < 0 ? "L" : "R"}${Math.round(Math.abs(pan) * 100)}`;
}

/**
 * The channel surface. Inspector and Rack were two views of the same thing —
 * one track's inserts, sends, channel and output — so they are one screen now:
 * the insert list is the chain, and the selected insert opens its stock Overlay
 * FX faceplate beside it. Nothing is duplicated, and nothing claims a write
 * route the bridge does not expose.
 */
export function Inspector({
  track,
  transport,
  onGain,
  onPan,
  onMute,
  onSolo,
  onArm,
  onSendA,
  onSendB,
  onInsert,
  channel,
  onChannelParam,
  onMidiLearn,
  compact = false,
  pitch,
  onPitch,
  instrumentPitch,
  onInstrumentPitch,
  pitchSync = false,
  onPitchSync,
}: InspectorProps) {
  // Hooks must run before the empty-track bail-out.
  const [focused, setFocused] = useState<number | null>(null);
  const [scale, setScale] = useState(0.65);
  // The preview starts fitted to its box; the +/- buttons take over once the
  // user picks a size, so a manual choice is not undone by the next resize.
  const [manualScale, setManualScale] = useState(false);
  const [bank, setBank] = useState(0);
  // Faceplate or parameters: the plug-in usually cannot show both at once on a
  // laptop screen, so the editor switches instead of scrolling the whole page.
  const [paramView, setParamView] = useState(false);
  const faceRef = useRef<HTMLDivElement | null>(null);

  // The faceplate is a preview, not a control surface. `inert` removes its
  // descendants from focus order as well as from the accessibility tree, which
  // aria-hidden alone does not do.
  // paramView remounts the faceplate; without it in the deps a fresh element
  // came back focusable behind the parameters view.
  useEffect(() => {
    faceRef.current?.setAttribute("inert", "");
  }, [focusedModuleKey(focused), paramView]);

  // Computed before the empty-track bail-out so the fit effect's dependency
  // list stays above the early return.
  const inserts = track?.inserts ?? [];
  const slots: readonly number[] = INSERT_BANKS[bank].slots;
  const firstFilled = slots.find((slot) => inserts[slot]?.name) ?? null;
  const focusedSlot = focused !== null && slots.includes(focused) ? focused : firstFilled;
  const focusedInsert = focusedSlot !== null ? inserts[focusedSlot] : undefined;
  const focusedModule = moduleForInsert(focusedInsert?.name);

  // Fit the faceplate to the editor box. Without this the 1320x780 preview
  // starts at a fixed scale and its lower half is cropped by the panel.
  // Hooks must run before the empty-track bail-out below.
  useEffect(() => {
    const box = faceRef.current;

    if (box === null || paramView || manualScale) return;

    const fit = () => {
      const rect = box.getBoundingClientRect();

      if (rect.width < 60 || rect.height < 60) return;

      const padding = 16; // .ws-editor-face padding
      setScale(fitScale(rect.width - padding, rect.height - padding, PANEL_W, PANEL_H));
    };

    fit();

    if (typeof ResizeObserver === "undefined") return;

    const observer = new ResizeObserver(fit);
    observer.observe(box);

    return () => observer.disconnect();
  }, [paramView, manualScale, focusedModule]);

  if (!track) {
    return <div className="ws-empty">No track selected</div>;
  }

  const peak = transport?.trackPeaks?.[track.index] ?? 0;
  const rows = focusedModule ? previewReadout(focusedModule, 40) : [];
  const zoom = (delta: number) => {
    setManualScale(true);
    setScale((value) => Math.max(0.35, Math.min(0.7, Math.round((value + delta) * 100) / 100)));
  };

  const column = (
    <>
      <header
        className="ws-ins-head"
        style={{ "--tint": tintForTrack(track.index) } as React.CSSProperties}
      >
        <span className="ws-tint" style={{ background: tintForTrack(track.index) }} />
        <p className="ws-ins-title">
          <span className="ws-ins-name" title={track.name}>
            {track.name}
          </span>
          <span className="ws-ins-sub">
            TRACK {track.index + 1}
            {track.instrumentName ? ` · ${track.instrumentName}` : ""}
          </span>
        </p>

        {!compact && (
          <span className="ws-strip-buttons">
            <button
              className="ws-mini-btn is-rec"
              aria-pressed={track.armed ?? false}
              aria-label={`Arm ${track.name}`}
              onClick={() => onArm(track.index, !track.armed)}
            >
              R
            </button>
            <button
              className="ws-mini-btn"
              aria-pressed={track.muted}
              aria-label={`Mute ${track.name}`}
              onClick={() => onMute(track.index, !track.muted)}
            >
              M
            </button>
            <button
              className="ws-mini-btn is-solo"
              aria-pressed={track.soloed}
              aria-label={`Solo ${track.name}`}
              onClick={() => onSolo(track.index, !track.soloed)}
            >
              S
            </button>
          </span>
        )}

        <span
          className="ws-ins-meter"
          title={`Peak ${dbLabel(20 * Math.log10(Math.max(1e-6, peak)))} dB`}
        >
          <i style={{ "--lvl": `${meterPct(peak)}%` } as React.CSSProperties} />
        </span>
      </header>

      <section className="ws-ins-card">
        <div className="ws-ins-card-head">
          <h3>Inserts</h3>
          <span className="ws-transport-group" role="group" aria-label="Insert bank">
            {INSERT_BANKS.map((entry, index) => (
              <button
                key={entry.id}
                className="ws-mini-btn"
                aria-pressed={bank === index}
                aria-label={`Show inserts ${entry.label}`}
                onClick={() => {
                  setBank(index);
                  setFocused(null);
                }}
              >
                {entry.label}
              </button>
            ))}
          </span>
        </div>

        <div className="ws-ins-slots">
          {slots.map((slot) => {
            const insert = inserts[slot];
            const filled = Boolean(insert?.name);
            const isFocused = focusedSlot === slot && filled;
            const className = [
              "ws-ins-slot",
              filled ? "is-filled" : "",
              insert?.missing ? "is-missing" : "",
              isFocused ? "is-focused" : "",
            ]
              .filter(Boolean)
              .join(" ");
            const label = insert?.missing
              ? `${insert.name} — missing`
              : insert?.name || `Insert ${slot + 1} — empty`;

            if (!onInsert && !filled) {
              // No load route yet: a disabled control is honest, a dead button is not.
              return (
                <span key={slot} className={className} aria-disabled="true" title={label}>
                  <span className="ws-ins-slot-no">{slot + 1}</span>
                  <span className="ws-ins-slot-name">{insert?.name || "empty"}</span>
                </span>
              );
            }

            return (
              <button
                key={slot}
                className={className}
                title={filled ? `${label} — opens its faceplate` : `${label} — click to load`}
                aria-label={label}
                aria-pressed={filled ? isFocused : undefined}
                onClick={() => {
                  if (filled) setFocused(slot);
                  else onInsert?.(slot);
                }}
              >
                <span className="ws-ins-slot-no">{slot + 1}</span>
                <span className="ws-ins-slot-name">{insert?.name || "empty"}</span>
                {filled && <i className="ws-ins-slot-led" aria-hidden="true" />}
              </button>
            );
          })}
        </div>
      </section>

      <section className="ws-ins-card">
        <h3>Sends</h3>
        <div className="ws-ins-knobs">
          <Knob
            label="Reverb"
            size="m"
            showValue
            spec={SEND_SPEC}
            value={toNorm((track.sendA ?? 0) * 100, SEND_SPEC)}
            defaultValue={toNorm(0, SEND_SPEC)}
            onChange={(v) => onSendA(track.index, Number((toValue(v, SEND_SPEC) / 100).toFixed(3)))}
          />
          <Knob
            label="Delay"
            size="m"
            showValue
            spec={SEND_SPEC}
            value={toNorm((track.sendB ?? 0) * 100, SEND_SPEC)}
            defaultValue={toNorm(0, SEND_SPEC)}
            onChange={(v) => onSendB(track.index, Number((toValue(v, SEND_SPEC) / 100).toFixed(3)))}
          />
        </div>
      </section>

      {channel && onChannelParam && (
        <section className="ws-ins-card">
          <h3>Strip</h3>
          <div className="ws-ins-knobs">
            <Knob
              label="HPF"
              size="m"
              showValue
              spec={HPF_SPEC}
              value={toNorm(channel.hpfFreq, HPF_SPEC)}
              defaultValue={toNorm(80, HPF_SPEC)}
              onChange={(v) => onChannelParam("hpfFreq", Number(toValue(v, HPF_SPEC).toFixed(0)))}
            />
            <Knob
              label="Comp"
              size="m"
              showValue
              spec={THRESH_SPEC}
              value={toNorm(channel.compThresh, THRESH_SPEC)}
              defaultValue={toNorm(0, THRESH_SPEC)}
              onChange={(v) =>
                onChannelParam("compThresh", Number(toValue(v, THRESH_SPEC).toFixed(1)))
              }
            />
            <Knob
              label="Out"
              size="m"
              showValue
              spec={OUTPUT_SPEC}
              value={toNorm(channel.outputDb, OUTPUT_SPEC)}
              defaultValue={toNorm(0, OUTPUT_SPEC)}
              onChange={(v) =>
                onChannelParam("outputDb", Number(toValue(v, OUTPUT_SPEC).toFixed(1)))
              }
            />
          </div>
          {onMidiLearn && (
            <div className="ws-ins-knobs">
              {(["hpfFreq", "compThresh", "outputDb"] as const).map((id) => (
                <button
                  key={id}
                  type="button"
                  className="ws-mini-btn"
                  title={`Arm MIDI learn for ${id}`}
                  onClick={() => onMidiLearn(id)}
                >
                  LEARN {id}
                </button>
              ))}
            </div>
          )}
        </section>
      )}

      <section className="ws-ins-card">
        <h3>Channel</h3>
        <div className="ws-ins-knobs">
          <Knob
            label="Gain"
            size={compact ? "m" : "l"}
            showValue
            spec={GAIN_SPEC}
            value={toNorm(track.gainDb, GAIN_SPEC)}
            defaultValue={toNorm(0, GAIN_SPEC)}
            onChange={(v) => onGain(track.index, Number(toValue(v, GAIN_SPEC).toFixed(1)))}
          />
          <Knob
            label="Pan"
            size={compact ? "m" : "l"}
            showValue
            bipolar
            spec={PAN_SPEC}
            value={toNorm(track.pan, PAN_SPEC)}
            defaultValue={toNorm(0, PAN_SPEC)}
            onChange={(v) => onPan(track.index, Number(toValue(v, PAN_SPEC).toFixed(2)))}
          />
        </div>
        <dl className="ws-ins-read">
          <div>
            <dt>Gain</dt>
            <dd>{dbLabel(track.gainDb)} dB</dd>
          </div>
          <div>
            <dt>Pan</dt>
            <dd>{panLabel(track.pan)}</dd>
          </div>
          <div>
            <dt>Width</dt>
            <dd>{track.width === undefined ? "—" : `${Math.round(track.width)}%`}</dd>
          </div>
        </dl>
      </section>

      <section className="ws-ins-card">
        <h3>Output</h3>
        <div className="ws-ins-route">
          <span className="ws-ins-route-node" title={track.name}>
            TRACK {track.index + 1}
          </span>
          <span className="ws-ins-route-arrow">→</span>
          <span className="ws-ins-route-node">MASTER</span>
        </div>
        {pitch && onPitch && (
          <div className="ws-ins-pitch">
            <span className="ws-ins-pitch-label" title={`${track.name} PITCH`}>
              TRACK {track.index + 1} PITCH
            </span>
            <PitchKnobs label={`${track.name} track`} value={pitch} onChange={onPitch} compact />
          </div>
        )}
        {instrumentPitch && onInstrumentPitch && (
          <div className="ws-ins-pitch">
            <span className="ws-ins-pitch-label">INSTRUMENT</span>
            <PitchKnobs
              label={`${track.name} instrument`}
              value={instrumentPitch}
              onChange={onInstrumentPitch}
              compact
            />
            {onPitchSync && (
              <button
                className="ws-btn"
                aria-pressed={pitchSync}
                aria-label="Instrument follows track pitch"
                title="Keep the instrument in tune with this track's pitch stack"
                onClick={() => onPitchSync(!pitchSync)}
              >
                {pitchSync ? "SYNCED" : "SYNC"}
              </button>
            )}
          </div>
        )}
      </section>
    </>
  );

  if (compact) {
    return <div className="ws-inspector is-compact">{column}</div>;
  }

  return (
    <div className="ws-channel">
      <div className="ws-channel-col">{column}</div>

      <section className="ws-channel-main" aria-label="Insert editor" style={{ minHeight: 0 }}>
        <header className="ws-editor-head">
          <span className="ws-editor-title">{focusedInsert?.name || "No insert selected"}</span>
          {focusedModule && <span className="ws-editor-tag">{moduleById(focusedModule).tag}</span>}
          {focusedModule && (
            <button
              className="ws-btn"
              aria-pressed={paramView}
              title={paramView ? "Show the faceplate" : "Show every parameter"}
              onClick={() => setParamView((value) => !value)}
            >
              {paramView ? "FACE" : "PARAMS"}
            </button>
          )}
          <span className="ws-editor-scale">
            <button
              className="ws-btn is-icon"
              aria-label="Shrink plug-in editor"
              title="Shrink the faceplate"
              onClick={() => zoom(-0.05)}
            >
              −
            </button>
            <span>{Math.round(scale * 100)}%</span>
            <button
              className="ws-btn is-icon"
              aria-label="Enlarge plug-in editor"
              title="Enlarge the faceplate"
              onClick={() => zoom(0.05)}
            >
              +
            </button>
          </span>
        </header>

        {focusedModule ? (
          // One view at a time: the faceplate owns the editor area, and PARAMS
          // swaps it for the full list. Parking parameters below the faceplate
          // shrank it to a strip and cropped the plug-in.
          paramView ? (
            <div className="ws-editor-params" aria-label="Plug-in parameters">
              {rows.map((row) => (
                <div className="ws-editor-param" key={row.key}>
                  <span>{row.label}</span>
                  <b>{row.value}</b>
                </div>
              ))}
            </div>
          ) : (
            <div className="ws-editor-face" aria-hidden="true" ref={faceRef}>
              <OverlayFace id={focusedModule} scale={scale} label={focusedInsert?.name} />
            </div>
          )
        ) : (
          <div className="ws-editor-empty">
            {focusedInsert?.name
              ? "This plug-in opens in the browser. Its controls are not shown here."
              : "Select an insert on the left. Its faceplate opens here."}
          </div>
        )}

        {onInsert && focusedSlot !== null && (
          <button className="ws-btn ws-editor-load" onClick={() => onInsert(focusedSlot)}>
            LOAD / REPLACE
          </button>
        )}
      </section>
    </div>
  );
}
