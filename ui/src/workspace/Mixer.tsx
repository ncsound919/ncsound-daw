import { useEffect, useRef, useState, type CSSProperties, type KeyboardEvent } from "react";
import type { InsertSlot, TrackInfo, TransportState } from "../types";
import { INSERT_BANKS, dbLabel, meterPct, tintForTrack } from "./format";
import { Knob } from "../console-ui/Knob";
import { toNorm, toValue, type ParamSpec } from "../console-ui/param";
import { densityFor, stripMinWidthPx } from "./mixerLayout";

const PAN_SPEC: ParamSpec = { min: -1, max: 1, decimals: 2 };
const SEND_SPEC: ParamSpec = { min: 0, max: 100, decimals: 0, unit: "%" };

export interface MixerProps {
  tracks: TrackInfo[];
  /** Viewport width in px. The app leaves it undefined; tests pin it. */
  viewportWidth?: number;
  selectedTrack: number;
  transport?: TransportState;
  masterGainDb: number;
  onSelectTrack: (track: number) => void;
  onGain: (track: number, db: number) => void;
  onPan: (track: number, pan: number) => void;
  onMute: (track: number, muted: boolean) => void;
  onSolo: (track: number, soloed: boolean) => void;
  onSendA: (track: number, amount: number) => void;
  onSendB: (track: number, amount: number) => void;
  onArm: (track: number, armed: boolean) => void;
  onMasterGain: (db: number) => void;
  onMasterDim: (on: boolean) => void;
  onMasterMono: (on: boolean) => void;
  onRack: (track: number) => void;
  onInsert?: (track: number, slot: number) => void;
  /** Alt-click a slot to bypass it without unloading the plug-in. */
  onBypass?: (track: number, slot: number, bypassed: boolean) => void;
  /** Drag a loaded slot onto an empty slot to reorder the chain. */
  onMove?: (track: number, from: number, to: number) => void;
  /** Double-click a slot to open the hosted plug-in editor. */
  onOpenEditor?: (track: number, slot: number) => Promise<{ ok: boolean; error?: string }>;
  onMasterRack?: () => void;
  onMasterInsert?: () => void;
  /** Opens the live baked channel-strip faceplate. */
  onOpenStrip?: () => void;
  /** Opens the live baked master faceplate. */
  onOpenMasterPanel?: () => void;
  /** Per-track AUTO switch; the button hides when the host has no route. */
  onAutomationRead?: (track: number, enabled: boolean) => void;
  /** Mixology technique presets; the section hides when the host has no route. */
  mixologyPresets?: string[];
  onApplyMixology?: (index: number) => Promise<string>;
  /** Render/measure the mix and apply an autonomous first pass. */
  onMixologyAutopilot?: (
    brief: string,
    depth: number,
    referencePath: string,
  ) => Promise<{ report: string; summary: string; assessment: string }>;
}

const DB_MIN = -60;
const DB_MAX = 6;

/**
 * Fader travel is compressed above -12 dB, the way a real console fader is:
 * unity sits at ~92% of the throw, so the useful mix range gets most of the
 * resolution instead of the first 20 dB of silence.
 */
function dbToNorm(db: number): number {
  const value = Math.max(DB_MIN, Math.min(DB_MAX, db));
  if (value <= -12) return ((value + 60) / 48) * 0.75;
  return 0.75 + ((value + 12) / 18) * 0.25;
}

function normToDb(norm: number): number {
  const value = Math.max(0, Math.min(1, norm));
  return value <= 0.75 ? -60 + (value / 0.75) * 48 : -12 + ((value - 0.75) / 0.25) * 18;
}

const SCALE_DB = [0, -12, -24, -48, -60];
/* Meter labels follow the peak law (sqrt), not the fader throw — see
   meterPct. 0 dB would sit at 100% and clip, so the top is left to the
   red zone + clip LED. */
const METER_DB = [-12, -24, -48];

/** Vertical fader: pointer drag, wheel, keyboard, double-click to unity. */
function Fader({
  value,
  label,
  tint,
  onChange,
}: {
  value: number;
  label: string;
  tint?: string;
  onChange: (db: number) => void;
}) {
  const rail = useRef<HTMLDivElement>(null);
  const [dragging, setDragging] = useState(false);
  const changeRef = useRef(onChange);
  changeRef.current = onChange;
  const detach = useRef<(() => void) | null>(null);

  // Pointer drags attach window listeners. Unmounting mid-drag must remove
  // them, otherwise the next pointer move calls into a dead component.
  useEffect(() => () => detach.current?.(), []);

  const commit = (clientY: number) => {
    const node = rail.current;
    if (!node) return;
    const rect = node.getBoundingClientRect();
    if (rect.height <= 0) return;
    const norm = 1 - (clientY - rect.top) / rect.height;
    const db = Math.round(normToDb(norm) * 10) / 10;
    changeRef.current(Math.max(DB_MIN, Math.min(DB_MAX, db)));
  };

  const beginDrag = (event: React.PointerEvent<HTMLDivElement>) => {
    event.stopPropagation();
    event.preventDefault();
    event.currentTarget.focus();
    detach.current?.();
    setDragging(true);
    commit(event.clientY);

    const move = (moveEvent: PointerEvent) => commit(moveEvent.clientY);
    const cleanup = () => {
      window.removeEventListener("pointermove", move);
      window.removeEventListener("pointerup", end);
      window.removeEventListener("pointercancel", end);
      detach.current = null;
    };
    const end = () => {
      setDragging(false);
      cleanup();
    };
    detach.current = cleanup;
    window.addEventListener("pointermove", move);
    window.addEventListener("pointerup", end);
    window.addEventListener("pointercancel", end);
  };

  const onKeyDown = (event: KeyboardEvent<HTMLDivElement>) => {
    const step = event.shiftKey ? 0.1 : 0.5;
    let next = value;
    if (event.key === "ArrowUp") next = value + step;
    else if (event.key === "ArrowDown") next = value - step;
    else if (event.key === "PageUp") next = value + 6;
    else if (event.key === "PageDown") next = value - 6;
    else if (event.key === "Home") next = DB_MIN;
    else if (event.key === "End") next = DB_MAX;
    else return;
    event.preventDefault();
    event.stopPropagation();
    changeRef.current(Math.max(DB_MIN, Math.min(DB_MAX, Math.round(next * 10) / 10)));
  };

  const norm = dbToNorm(value);

  return (
    <div className="ws-fader-wrap" onClick={(event) => event.stopPropagation()}>
      <div className="ws-fader-scale" aria-hidden="true">
        {SCALE_DB.map((db) => (
          <span
            key={db}
            className={db === 0 ? "is-unity" : undefined}
            /* Same inset geometry as the cap (4px top/bottom, 26px travel),
               so a label always sits on its tick and on the cap line. */
            style={{ bottom: `calc(13px + ${dbToNorm(db)} * (100% - 26px))` }}
          >
            {db > 0 ? `+${db}` : db}
          </span>
        ))}
      </div>

      <div
        ref={rail}
        className={dragging ? "ws-fader is-dragging" : "ws-fader"}
        role="slider"
        tabIndex={0}
        aria-label={label}
        aria-orientation="vertical"
        aria-valuemin={DB_MIN}
        aria-valuemax={DB_MAX}
        aria-valuenow={value}
        aria-valuetext={`${dbLabel(value)} dB`}
        onPointerDown={beginDrag}
        onWheel={(event) => {
          event.stopPropagation();
          onChange(Math.max(DB_MIN, Math.min(DB_MAX, value + (event.deltaY < 0 ? 0.5 : -0.5))));
        }}
        onDoubleClick={(event) => {
          event.stopPropagation();
          onChange(0);
        }}
        onKeyDown={onKeyDown}
        style={{ "--t": norm, "--tint": tint ?? "rgba(248, 232, 200, 0.5)" } as CSSProperties}
      >
        <span className="ws-fader-groove" />
        <span className="ws-fader-unity" aria-hidden="true" />
        <span className="ws-fader-cap" />
      </div>
    </div>
  );
}

/** Stereo peak meter: segmented ladder, clip LED and a decaying peak hold. */
function Meter({ left, right, title }: { left: number; right: number; title: string }) {
  const peak = Math.max(left, right);
  const [hold, setHold] = useState(peak);

  useEffect(() => {
    if (peak >= hold) {
      setHold(peak);
      return;
    }
    const timer = window.setTimeout(() => setHold(peak), 900);
    return () => window.clearTimeout(timer);
  }, [peak, hold]);

  const l = meterPct(left);
  const r = meterPct(right);
  return (
    <div className="ws-meters" title={title} aria-hidden="true">
      <span
        className="ws-meter-hold"
        style={{ bottom: `calc(4px + ${meterPct(hold) / 100} * (100% - 8px))` }}
      />
      <span
        className={left >= 0.985 ? "ws-meter-col is-clip" : "ws-meter-col"}
        style={{ "--lvl": `${l}%` } as CSSProperties}
      />
      <span
        className={right >= 0.985 ? "ws-meter-col is-clip" : "ws-meter-col"}
        style={{ "--lvl": `${r}%` } as CSSProperties}
      />
    </div>
  );
}

/** Console dB scale beside the meters — positions use the same sqrt peak
 *  law as the fill (meterPct), so -12 reads 50% where it fills 50%. */
function MeterScale() {
  return (
    <div className="ws-meter-scale" aria-hidden="true">
      {METER_DB.map((db) => (
        <span
          key={db}
          style={{ bottom: `calc(4px + ${meterPct(10 ** (db / 20))}% * (100% - 8px) / 100%)` }}
        >
          {db}
        </span>
      ))}
    </div>
  );
}

function SlotRow({
  insert,
  slot,
  track,
  onInsert,
  onBypass,
  onMove,
  onOpenEditor,
}: {
  insert?: InsertSlot;
  slot: number;
  track: number;
  onInsert?: (track: number, slot: number) => void;
  /** Alt-click a slot to bypass it without unloading the plug-in. */
  onBypass?: (track: number, slot: number, bypassed: boolean) => void;
  /** Drag a loaded slot onto an empty slot to reorder the chain. */
  onMove?: (track: number, from: number, to: number) => void;
  /** Double-click a slot to open the hosted plug-in editor. */
  onOpenEditor?: (track: number, slot: number) => void;
}) {
  const filled = Boolean(insert?.name);
  const className = [
    "ws-slot",
    filled ? "is-filled" : "",
    insert?.missing ? "is-missing" : "",
    insert?.bypassed ? "is-bypassed" : "",
  ]
    .filter(Boolean)
    .join(" ");
  const label = insert?.name ? insert.name.replace(/^Overlay\s+/i, "") : "empty";
  const name = insert?.missing ? `${label} — MISSING` : label;
  const title = insert?.missing
    ? `${insert.name} is missing`
    : insert?.name || `Insert ${slot + 1} is empty`;
  const hint = onBypass && filled ? " — alt-click to bypass" : "";

  return (
    <button
      className={className}
      title={onInsert ? `${title} — click to load${hint}` : title}
      aria-label={`Insert ${slot + 1} ${title}`}
      aria-pressed={insert?.bypassed ? false : undefined}
      disabled={!onInsert && !onBypass && !onOpenEditor}
      draggable={filled && Boolean(onMove)}
      onDoubleClick={(event) => {
        event.stopPropagation();
        onOpenEditor?.(track, slot);
      }}
      onDragStart={(event) => {
        if (filled && onMove)
          event.dataTransfer.setData("application/x-nc-insert", `${track}:${slot}`);
      }}
      onDragOver={(event) => {
        if (onMove) event.preventDefault();
      }}
      onDrop={(event) => {
        event.preventDefault();
        if (!onMove) return;
        const raw = event.dataTransfer.getData("application/x-nc-insert").split(":");
        if (raw.length !== 2) return;
        const fromTrack = Number(raw[0]);
        const fromSlot = Number(raw[1]);
        if (fromTrack !== track || fromSlot === slot) return;
        // Never overwrite a loaded slot: the native route refuses too, but the
        // refusal the user sees has to happen before any round trip.
        if (filled) return;
        onMove(track, fromSlot, slot);
      }}
      onClick={(event) => {
        event.stopPropagation();

        if (event.altKey) {
          event.preventDefault();
          onBypass?.(track, slot, !insert?.bypassed);
          return;
        }

        onInsert?.(track, slot);
      }}
    >
      <i className="ws-slot-led" aria-hidden="true" />
      <span className="ws-slot-no" aria-hidden="true">
        {slot + 1}
      </span>
      <span className="ws-slot-name">{name}</span>
    </button>
  );
}

function MixKnobs({
  track,
  onPan,
  onSendA,
  onSendB,
}: {
  track: TrackInfo;
  onPan: (pan: number) => void;
  onSendA: (amount: number) => void;
  onSendB: (amount: number) => void;
}) {
  return (
    <div className="ws-mix-knobs" onClick={(event) => event.stopPropagation()}>
      <Knob
        label="Pan"
        size="m"
        showValue={false}
        bipolar
        spec={PAN_SPEC}
        value={toNorm(track.pan, PAN_SPEC)}
        defaultValue={toNorm(0, PAN_SPEC)}
        onChange={(v) => onPan(Number(toValue(v, PAN_SPEC).toFixed(2)))}
      />
      <Knob
        label="Reverb"
        size="m"
        showValue={false}
        spec={SEND_SPEC}
        value={toNorm((track.sendA ?? 0) * 100, SEND_SPEC)}
        defaultValue={toNorm(0, SEND_SPEC)}
        onChange={(v) => onSendA(Number((toValue(v, SEND_SPEC) / 100).toFixed(3)))}
      />
      <Knob
        label="Delay"
        size="m"
        showValue={false}
        spec={SEND_SPEC}
        value={toNorm((track.sendB ?? 0) * 100, SEND_SPEC)}
        defaultValue={toNorm(0, SEND_SPEC)}
        onChange={(v) => onSendB(Number((toValue(v, SEND_SPEC) / 100).toFixed(3)))}
      />
    </div>
  );
}

type VizMode = "spectrum" | "lufs" | "stereo";

function lufsOf(peak: number): number {
  if (peak <= 0.001) return Number.NEGATIVE_INFINITY;
  return 20 * Math.log10(peak) - 3;
}

function masteringTips(
  masterGainDb: number,
  peakDb: number,
  lufs: number,
  dim: boolean,
  mono: boolean,
): { text: string; warn: boolean }[] {
  const tips: { text: string; warn: boolean }[] = [];
  if (!Number.isFinite(peakDb)) {
    tips.push({ text: "No signal yet — play the arrangement to measure the master.", warn: false });
  } else if (peakDb > -1) {
    tips.push({
      text: `Peak at ${dbLabel(peakDb)} dBFS is over −1. Exit the limiter / lower master gain.`,
      warn: true,
    });
  } else if (peakDb > -3) {
    tips.push({
      text: `Peak ${dbLabel(peakDb)} dBFS — leave 1 dB of true-peak headroom for streaming codecs.`,
      warn: false,
    });
  } else {
    tips.push({
      text: `Peak ${dbLabel(peakDb)} dBFS is inside the −1 dB streaming ceiling.`,
      warn: false,
    });
  }
  if (Number.isFinite(lufs)) {
    if (lufs < -16)
      tips.push({
        text: `Integrated ${lufs.toFixed(1)} LUFS — quiet for streaming; raise master gain toward −14 LUFS.`,
        warn: false,
      });
    else if (lufs > -8)
      tips.push({
        text: `Integrated ${lufs.toFixed(1)} LUFS is hot — lossy codecs will pump. Ease the limiter.`,
        warn: true,
      });
    else
      tips.push({
        text: `Integrated ${lufs.toFixed(1)} LUFS sits in the streaming window (−14 to −10 LUFS).`,
        warn: false,
      });
  }
  if (masterGainDb > 3)
    tips.push({
      text: `Master gain +${masterGainDb.toFixed(1)} dB is pushing the limiter — check gain staging.`,
      warn: true,
    });
  if (dim) tips.push({ text: "DIM is on — audition at −20 dB, then check mono.", warn: false });
  if (mono)
    tips.push({
      text: "MONO is on — collapse hides stereo effects; audition in stereo before delivery.",
      warn: true,
    });
  tips.push({
    text: "Industry target: −14 LUFS integrated, −1 dBTP ceiling. Print at 16-bit / 48 kHz for delivery.",
    warn: false,
  });
  return tips;
}

function MasterViz({
  masterGainDb,
  masterPeakLeft,
  masterPeakRight,
  dim,
  mono,
}: {
  masterGainDb: number;
  masterPeakLeft: number;
  masterPeakRight: number;
  dim: boolean;
  mono: boolean;
}) {
  const [mode, setMode] = useState<VizMode>("spectrum");
  const [large, setLarge] = useState(false);
  const canvasRef = useRef<HTMLCanvasElement>(null);
  const peak = Math.max(masterPeakLeft, masterPeakRight);
  const peakDb = peak > 0 ? 20 * Math.log10(Math.max(1e-6, peak)) : Number.NEGATIVE_INFINITY;
  const lufs = lufsOf(peak);
  const tips = masteringTips(masterGainDb, peakDb, lufs, dim, mono);

  useEffect(() => {
    const canvas = canvasRef.current;
    if (!canvas) return;
    const ctx = canvas.getContext("2d");
    if (!ctx) return;
    const rect = canvas.getBoundingClientRect();
    canvas.width = Math.max(1, rect.width);
    canvas.height = Math.max(1, rect.height);
    const w = canvas.width;
    const h = canvas.height;
    ctx.clearRect(0, 0, w, h);
    ctx.fillStyle = "#0b0a09";
    ctx.fillRect(0, 0, w, h);
    if (mode === "spectrum") {
      const bars = 48;
      for (let i = 0; i < bars; i += 1) {
        const t = i / (bars - 1);
        const level = Math.max(
          0,
          Math.min(
            1,
            Math.sqrt(Math.max(masterPeakLeft, masterPeakRight)) *
              (1 - t * 0.72) *
              (0.72 + 0.28 * Math.sin(t * 6.28 + performance.now() / 900)),
          ),
        );
        const barH = level * h;
        ctx.fillStyle = level > 0.84 ? "#e0503c" : level > 0.6 ? "#e0824d" : "#f8e8c8";
        ctx.fillRect((i / bars) * w + 1, h - barH, w / bars - 2, barH);
      }
      ctx.fillStyle = "rgba(248,232,200,0.8)";
      ctx.font = "11px monospace";
      ctx.fillText(`${dbLabel(peakDb)} dBFS`, 8, 16);
    } else if (mode === "lufs") {
      const pos = Number.isFinite(lufs) ? Math.max(0, Math.min(1, (lufs + 30) / 30)) : 0;
      const x = pos * w;
      ctx.strokeStyle = "#e0824d";
      ctx.lineWidth = 1.5;
      ctx.beginPath();
      ctx.moveTo(x, 8);
      ctx.lineTo(x, h - 8);
      ctx.stroke();
      ctx.fillStyle = "#f8e8c8";
      ctx.font = "11px monospace";
      ctx.fillText(Number.isFinite(lufs) ? `${lufs.toFixed(1)} LUFS` : "— LUFS", 8, 16);
      ctx.fillStyle = "rgba(248,232,200,0.5)";
      ctx.fillText("−30", 8, h - 8);
      ctx.fillText("0", w - 16, h - 8);
    } else {
      const corr = mono ? 1 : Math.max(0, Math.min(1, Math.abs(masterPeakLeft - masterPeakRight)));
      ctx.strokeStyle = "rgba(248,232,200,0.8)";
      ctx.beginPath();
      ctx.moveTo(w / 2, 8);
      ctx.lineTo(w * (0.5 - corr * 0.42), h - 12);
      ctx.moveTo(w / 2, 8);
      ctx.lineTo(w * (0.5 + corr * 0.42), h - 12);
      ctx.stroke();
      ctx.fillStyle = "#f8e8c8";
      ctx.font = "11px monospace";
      ctx.fillText(mono ? "MONO" : "STEREO", 8, 16);
    }
  }, [mode, masterPeakLeft, masterPeakRight, peakDb, lufs, mono, large]);

  const openLarge = () => setLarge(true);
  const cycleMode = () => {
    setMode((m) => (m === "spectrum" ? "lufs" : m === "lufs" ? "stereo" : "spectrum"));
  };
  const viz = (
    <>
      <div
        className="ws-master-viz-canvas"
        role="button"
        tabIndex={0}
        title="Click to change readout · double-click for larger view"
        aria-label={`Master ${mode} readout — click to change`}
        onClick={(event) => {
          if (event.detail >= 2) return;
          cycleMode();
        }}
        onDoubleClick={openLarge}
        onKeyDown={(event) => {
          if (event.key === "Enter" || event.key === " ") {
            event.preventDefault();
            cycleMode();
          }
        }}
      >
        <canvas ref={canvasRef} aria-hidden="true" />
      </div>
      <ul className="ws-master-viz-tips">
        {tips.map((tip, i) => (
          <li key={i} className={tip.warn ? "is-warn" : ""}>
            {tip.text}
          </li>
        ))}
      </ul>
    </>
  );

  return (
    <>
      {!large && (
        <div
          className="ws-master-viz"
          title="Double-click for larger view"
          role="group"
          aria-label="Master visualizer"
          onDoubleClick={openLarge}
        >
          {viz}
        </div>
      )}
      {large && (
        <div
          className="ws-master-viz-modal"
          role="dialog"
          aria-modal="true"
          aria-label="Master visualizer large"
          onClick={() => setLarge(false)}
        >
          <div className="ws-master-viz-modal-panel" onClick={(event) => event.stopPropagation()}>
            <div className="ws-master-viz" style={{ flex: "1 1 auto" }}>
              {viz}
            </div>
            <button className="ws-btn" type="button" autoFocus onClick={() => setLarge(false)}>
              CLOSE
            </button>
          </div>
        </div>
      )}
    </>
  );
}

function ChannelStrip({
  track,
  selected,
  peak,
  onSelectTrack,
  onGain,
  onPan,
  onMute,
  onSolo,
  onSendA,
  onSendB,
  onArm,
  onRack,
  onInsert,
  onBypass,
  onMove,
  onOpenEditor,
  onAutomationRead,
}: {
  track: TrackInfo;
  selected: boolean;
  peak: number;
  onSelectTrack: () => void;
  onGain: (db: number) => void;
  onPan: (pan: number) => void;
  onMute: () => void;
  onSolo: () => void;
  onSendA: (amount: number) => void;
  onSendB: (amount: number) => void;
  onArm: () => void;
  onRack: () => void;
  onInsert?: (track: number, slot: number) => void;
  /** Alt-click a slot to bypass it without unloading the plug-in. */
  onBypass?: (track: number, slot: number, bypassed: boolean) => void;
  /** Drag a loaded slot onto an empty slot to reorder the chain. */
  onMove?: (track: number, from: number, to: number) => void;
  /** Double-click a slot to open the hosted plug-in editor. */
  onOpenEditor?: (track: number, slot: number) => Promise<{ ok: boolean; error?: string }>;
  onAutomationRead?: (track: number, enabled: boolean) => void;
}) {
  const tint = tintForTrack(track.index);
  const inserts = track.inserts ?? [];
  const [bank, setBank] = useState(0);
  const [editorError, setEditorError] = useState<string | null>(null);
  const peakDb = peak > 0 ? 20 * Math.log10(Math.max(1e-6, peak)) : Number.NEGATIVE_INFINITY;

  return (
    <section
      className="ws-strip"
      aria-current={selected ? "true" : undefined}
      aria-label={`${track.name} channel`}
      onClick={onSelectTrack}
      style={{ "--tint": tint } as CSSProperties}
    >
      <span className="ws-strip-tint" style={{ background: tint }} aria-hidden="true" />

      <header className="ws-strip-head">
        <span className="ws-strip-name" title={track.name}>
          TRACK {track.index + 1}
        </span>
      </header>

      <div className="ws-strip-section">
        <div className="ws-section-head">
          <span>Inserts {INSERT_BANKS[bank].label}</span>
          <button
            className="ws-mini-btn"
            aria-label="Switch insert bank"
            title="Show the other four insert slots"
            onClick={(event) => {
              event.stopPropagation();
              setBank((value) => (value + 1) % INSERT_BANKS.length);
            }}
          >
            {INSERT_BANKS[bank].label}
          </button>
          <i
            className={peak >= 0.985 ? "ws-strip-clip is-hot" : "ws-strip-clip"}
            aria-hidden="true"
          />
        </div>
        <div className="ws-slots">
          {INSERT_BANKS[bank].slots.map((slot) => (
            <SlotRow
              key={slot}
              slot={slot}
              insert={inserts[slot]}
              track={track.index}
              onInsert={onInsert}
              onBypass={onBypass}
              onMove={onMove}
              onOpenEditor={
                onOpenEditor
                  ? (slotTrack: number, slotIndex: number) => {
                      onOpenEditor(slotTrack, slotIndex).then(
                        (result) =>
                          setEditorError(result.ok ? null : (result.error ?? "Editor unavailable")),
                        () => setEditorError("Editor unavailable"),
                      );
                    }
                  : undefined
              }
            />
          ))}
        </div>
        {editorError && (
          <p className="ws-slot-error" role="alert">
            {editorError}
          </p>
        )}
      </div>

      <MixKnobs track={track} onPan={onPan} onSendA={onSendA} onSendB={onSendB} />

      <div className="ws-strip-fader">
        <Fader value={track.gainDb} label={`${track.name} gain`} tint={tint} onChange={onGain} />
        <Meter left={peak} right={peak} title={`${track.name} peak ${dbLabel(peakDb)} dBFS`} />
        <MeterScale />
      </div>

      <div className="ws-strip-read">
        <span className="ws-read-value">{dbLabel(track.gainDb)}</span>
        <span className="ws-read-unit">dB</span>
        <span className="ws-read-peak">{dbLabel(peakDb)}</span>
      </div>

      <div className="ws-strip-buttons" role="group" aria-label={`${track.name} channel switches`}>
        <button
          className="ws-chan-btn is-rec"
          aria-pressed={track.armed ?? false}
          aria-label={`Arm ${track.name}`}
          title={`Record-arm ${track.name}`}
          onClick={(event) => {
            event.stopPropagation();
            onArm();
          }}
        >
          R
        </button>
        <button
          className="ws-chan-btn"
          aria-pressed={track.muted}
          aria-label={`Mute ${track.name}`}
          title={`Mute ${track.name}`}
          onClick={(event) => {
            event.stopPropagation();
            onMute();
          }}
        >
          M
        </button>
        <button
          className="ws-chan-btn is-auto"
          aria-pressed={track.automationRead ?? true}
          aria-label={`AUTO ${track.name}`}
          title={
            (track.automationRead ?? true)
              ? `Automation follows the lanes on ${track.name}`
              : `Automation parked; the fader drives ${track.name}`
          }
          onClick={(event) => {
            event.stopPropagation();
            onAutomationRead?.(track.index, !(track.automationRead ?? true));
          }}
        >
          A
        </button>
        <button
          className="ws-chan-btn is-solo"
          aria-pressed={track.soloed}
          aria-label={`Solo ${track.name}`}
          title={`Solo ${track.name}`}
          onClick={(event) => {
            event.stopPropagation();
            onSolo();
          }}
        >
          S
        </button>
      </div>

      <div className="ws-strip-flow" aria-hidden="true">
        <span>IN</span>
        <i />
        <span>INS</span>
        <i />
        <span>SND</span>
        <i />
        <span>PAN</span>
        <i />
        <span>FDR</span>
      </div>

      <footer className="ws-strip-foot">
        <span className="ws-strip-out">OUT 1–2</span>
        <button
          className="ws-strip-dsp"
          onClick={(event) => {
            event.stopPropagation();
            onRack();
          }}
          title={`Open the channel rack for ${track.name}`}
          aria-label={`Rack ${track.name}`}
        >
          DSP
        </button>
      </footer>
    </section>
  );
}

export function Mixer({
  tracks,
  viewportWidth,
  selectedTrack,
  transport,
  masterGainDb,
  onSelectTrack,
  onGain,
  onPan,
  onMute,
  onSolo,
  onSendA,
  onSendB,
  onArm,
  onMasterGain,
  onMasterDim,
  onMasterMono,
  onRack,
  onInsert,
  onBypass,
  onMove,
  onOpenEditor,
  onAutomationRead,
  onMasterRack,
  onMasterInsert,
  onOpenStrip,
  onOpenMasterPanel,
  mixologyPresets,
  onApplyMixology,
  onMixologyAutopilot,
}: MixerProps) {
  const [mixologyReport, setMixologyReport] = useState<string | null>(null);
  const [autopilotBrief, setAutopilotBrief] = useState("Balance the mix and open the top end");
  const [autopilotReference, setAutopilotReference] = useState("");
  const [autopilotDepth, setAutopilotDepth] = useState(0.65);
  const [autopilotBusy, setAutopilotBusy] = useState(false);
  const [autopilotSummary, setAutopilotSummary] = useState<string | null>(null);

  const runAutopilot = () => {
    if (!onMixologyAutopilot || autopilotBusy) return;
    setAutopilotBusy(true);
    setAutopilotSummary(null);
    onMixologyAutopilot(autopilotBrief, autopilotDepth, autopilotReference.trim())
      .then(({ report, summary, assessment }) => {
        setMixologyReport(report);
        setAutopilotSummary([assessment, summary].filter(Boolean).join(" "));
      })
      .catch((error: unknown) => {
        setMixologyReport(error instanceof Error ? error.message : String(error));
      })
      .finally(() => setAutopilotBusy(false));
  };
  const masterPeak = Math.max(transport?.masterPeakLeft ?? 0, transport?.masterPeakRight ?? 0);
  const masterPeakDb =
    masterPeak > 0 ? 20 * Math.log10(Math.max(1e-6, masterPeak)) : Number.NEGATIVE_INFINITY;

  const [densityChoice, setDensityChoice] = useState<"auto" | "normal" | "compact">("auto");

  // A manual choice wins until the window changes; then the automatic decision takes
  // over again, so a resize cannot leave 16 strips stuck at the wide width.
  useEffect(() => {
    setDensityChoice("auto");
  }, [viewportWidth]);

  const autoDensity = densityFor(tracks.length, viewportWidth ?? window.innerWidth);
  const density = densityChoice === "auto" ? autoDensity : densityChoice;

  return (
    <div
      className="ws-mixer"
      role="group"
      aria-label="Mixer"
      data-density={density}
      style={{ "--strip-min": `${stripMinWidthPx(density)}px` } as CSSProperties}
    >
      <div className="ws-mixer-tools">
        {onOpenStrip && (
          <button type="button" className="ws-mini-btn" onClick={onOpenStrip}>
            Channel strip
          </button>
        )}
        {onOpenMasterPanel && (
          <button type="button" className="ws-mini-btn" onClick={onOpenMasterPanel}>
            Master rack
          </button>
        )}
        <button
          type="button"
          className="ws-mini-btn"
          aria-pressed={densityChoice !== "auto"}
          onClick={() => setDensityChoice(densityChoice === "auto" ? "normal" : "auto")}
        >
          Fit strips
        </button>
      </div>

      <div className="ws-mixbody">
        <div className="ws-strips">
          {tracks.map((track) => (
            <ChannelStrip
              key={track.index}
              track={track}
              selected={track.index === selectedTrack}
              peak={transport?.trackPeaks?.[track.index] ?? 0}
              onSelectTrack={() => onSelectTrack(track.index)}
              onGain={(db) => onGain(track.index, db)}
              onPan={(pan) => onPan(track.index, pan)}
              onMute={() => onMute(track.index, !track.muted)}
              onSolo={() => onSolo(track.index, !track.soloed)}
              onSendA={(amount) => onSendA(track.index, amount)}
              onSendB={(amount) => onSendB(track.index, amount)}
              onArm={() => onArm(track.index, !track.armed)}
              onRack={() => onRack(track.index)}
              onInsert={onInsert}
              onBypass={onBypass}
              onMove={onMove}
              onAutomationRead={onAutomationRead}
              onOpenEditor={onOpenEditor}
            />
          ))}
        </div>

        <section className="ws-strip is-master" aria-label="Master channel">
          <span className="ws-strip-tint is-master" aria-hidden="true" />

          <header className="ws-strip-head">
            <span className="ws-strip-name">MASTER</span>
            <span className="ws-strip-inst">OUT 1–2 · 24 BIT</span>
          </header>

          <div className="ws-strip-section">
            <div className="ws-section-head">
              <span>Master chain</span>
              <i
                className={masterPeak >= 0.985 ? "ws-strip-clip is-hot" : "ws-strip-clip"}
                aria-hidden="true"
              />
            </div>
            <div className="ws-slots">
              <button
                className="ws-slot is-filled"
                onClick={onMasterRack}
                title="Stock Overlay Master — open the channel rack"
                aria-label="Open master rack"
              >
                <i className="ws-slot-led" aria-hidden="true" />
                <span className="ws-slot-no">1</span>
                <span className="ws-slot-name">Overlay Master</span>
              </button>
              <button
                className="ws-slot"
                onClick={onMasterInsert}
                disabled={!onMasterInsert}
                title={
                  onMasterInsert ? "Load a VST into the master insert" : "Master insert unavailable"
                }
                aria-label="Load master insert"
              >
                <i className="ws-slot-led" aria-hidden="true" />
                <span className="ws-slot-no">2</span>
                <span className="ws-slot-name">Master VST</span>
              </button>
            </div>
          </div>

          {onMixologyAutopilot && (
            <div className="ws-strip-section" role="group" aria-label="Mixology autopilot">
              <div className="ws-section-head">
                <span>Autopilot</span>
              </div>
              <input
                className="ws-autopilot-brief"
                aria-label="Mixology autopilot brief"
                value={autopilotBrief}
                onChange={(event) => setAutopilotBrief(event.target.value)}
                placeholder="Brief, e.g. clearer vocal, heavier 808"
                style={{
                  width: "100%",
                  boxSizing: "border-box",
                  marginBottom: 6,
                  background: "transparent",
                  border: "1px solid var(--ws-line, #3a3a3a)",
                  color: "inherit",
                  font: "inherit",
                  padding: "4px 6px",
                }}
              />
              <input
                className="ws-autopilot-brief"
                aria-label="Mixology reference track path"
                value={autopilotReference}
                onChange={(event) => setAutopilotReference(event.target.value)}
                placeholder="Reference track path (optional)"
                style={{
                  width: "100%",
                  boxSizing: "border-box",
                  marginBottom: 6,
                  background: "transparent",
                  border: "1px solid var(--ws-line, #3a3a3a)",
                  color: "inherit",
                  font: "inherit",
                  padding: "4px 6px",
                }}
              />
              <div className="ws-strip-buttons is-master-switches">
                <button
                  type="button"
                  className="ws-chan-btn"
                  disabled={autopilotBusy}
                  aria-label="Run Mixology autopilot"
                  title="Render the session, measure it with Earshot, and apply a bounded first pass"
                  onClick={runAutopilot}
                >
                  {autopilotBusy ? "Analysing…" : "Autopilot"}
                </button>
                <button
                  type="button"
                  className="ws-chan-btn"
                  aria-label="Autopilot depth"
                  onClick={() =>
                    setAutopilotDepth(
                      autopilotDepth >= 0.85 ? 0.35 : autopilotDepth >= 0.65 ? 0.85 : 0.65,
                    )
                  }
                  title="Cycle autopilot depth: gentle, musical, loud"
                >
                  {autopilotDepth >= 0.85 ? "LOUD" : autopilotDepth >= 0.65 ? "MUSICAL" : "GENTLE"}
                </button>
              </div>
              {autopilotSummary && (
                <p className="ws-mixology-report" aria-live="polite">
                  {autopilotSummary}
                </p>
              )}
            </div>
          )}

          {mixologyPresets && mixologyPresets.length > 0 && onApplyMixology && (
            <div className="ws-strip-section" role="group" aria-label="Mixology presets">
              <div className="ws-section-head">
                <span>Mixology</span>
              </div>
              <div className="ws-strip-buttons is-master-switches">
                {mixologyPresets.map((name, index) => (
                  <button
                    key={name}
                    className="ws-chan-btn"
                    title={"Apply the " + name + " mix template"}
                    aria-label={"Apply Mixology preset " + name}
                    onClick={() => {
                      onApplyMixology(index).then(setMixologyReport).catch(console.error);
                    }}
                  >
                    {name}
                  </button>
                ))}
              </div>
              {mixologyReport && (
                <p className="ws-mixology-report" aria-live="polite">
                  {mixologyReport}
                </p>
              )}
            </div>
          )}

          <div
            className="ws-strip-buttons is-master-switches"
            role="group"
            aria-label="Master monitoring"
          >
            <button
              className="ws-chan-btn is-wide"
              aria-pressed={transport?.masterDim ?? false}
              onClick={() => onMasterDim(!(transport?.masterDim ?? false))}
              title="Dim the monitor output by 20 dB"
              aria-label="Dim master output"
            >
              DIM
            </button>
            <button
              className="ws-chan-btn is-wide"
              aria-pressed={transport?.masterMono ?? false}
              onClick={() => onMasterMono(!(transport?.masterMono ?? false))}
              title="Fold the monitor output to mono"
              aria-label="Mono master output"
            >
              MONO
            </button>
          </div>

          <div className="ws-strip-fader">
            <Fader
              value={masterGainDb}
              label="Master gain"
              tint="#f8e8c8"
              onChange={onMasterGain}
            />
            <Meter
              left={transport?.masterPeakLeft ?? 0}
              right={transport?.masterPeakRight ?? 0}
              title={`Master peak ${dbLabel(masterPeakDb)} dBFS`}
            />
            <MeterScale />
          </div>

          <div className="ws-strip-read is-master-read">
            <span className="ws-read-value">{dbLabel(masterGainDb)}</span>
            <span className="ws-read-unit">dB</span>
            <span className="ws-read-peak">{dbLabel(masterPeakDb)} dBFS</span>
          </div>

          <div
            className="ws-strip-buttons is-master-switches"
            role="group"
            aria-label="Master switches"
          >
            <button
              className="ws-chan-btn"
              disabled
              title="Master A/B — not available"
              aria-label="Master A/B"
            >
              A/B
            </button>
            <button
              className="ws-chan-btn"
              disabled
              title="Automix — not available"
              aria-label="Automix"
            >
              AUTO
            </button>
          </div>

          <div className="ws-strip-flow" aria-hidden="true">
            <span>IN</span>
            <i />
            <span>MAX</span>
            <i />
            <span>DIM</span>
            <i />
            <span>FDR</span>
            <i />
            <span>OUT</span>
          </div>

          <MasterViz
            masterGainDb={masterGainDb}
            masterPeakLeft={transport?.masterPeakLeft ?? 0}
            masterPeakRight={transport?.masterPeakRight ?? 0}
            dim={transport?.masterDim ?? false}
            mono={transport?.masterMono ?? false}
          />

          <footer className="ws-strip-foot">
            <span className="ws-strip-out">OUT 1–2</span>
          </footer>
        </section>
      </div>
    </div>
  );
}
