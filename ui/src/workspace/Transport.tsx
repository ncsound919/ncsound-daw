import { useRef } from "react";
import type { PitchState, TransportState } from "../types";
import { PitchKnobs } from "../components/PitchKnobs";
import { dbLabel, formatBarsBeats, meterPct } from "./format";

export interface TransportProps {
  transport: TransportState;
  onPlay: (playing: boolean) => void;
  onStop: () => void;
  onRewind: () => void;
  onBpm: (bpm: number) => void;
  onLoop: (on: boolean) => void;
  onMetronome: (on: boolean) => void;
  /** Starts and stops capture on the armed tracks (two bridge routes). */
  onRecord?: (recording: boolean) => void;
  recording?: boolean;
  /** Global pitch lives in the empty space beside the transport controls. */
  globalPitch?: PitchState;
  onGlobalPitch?: (patch: Partial<PitchState>) => void;
}

export function Transport({
  transport,
  onPlay,
  onStop,
  onRewind,
  onBpm,
  onLoop,
  onMetronome,
  onRecord,
  recording = false,
  globalPitch,
  onGlobalPitch,
}: TransportProps) {
  const drag = useRef<{ y: number; bpm: number } | null>(null);
  const position = formatBarsBeats(transport.positionBar);
  // Peak is the louder channel, not the average: a single hot side reads hot.
  // Silence reads as an em dash, not a fabricated -60 dB.
  const masterPeak = Math.max(transport.masterPeakLeft, transport.masterPeakRight);
  const masterDb = masterPeak <= 0.001 ? "—" : dbLabel(20 * Math.log10(masterPeak));

  const beginBpmDrag = (event: React.PointerEvent<HTMLDivElement>) => {
    drag.current = { y: event.clientY, bpm: transport.bpm };
    event.currentTarget.setPointerCapture(event.pointerId);
  };

  const moveBpmDrag = (event: React.PointerEvent<HTMLDivElement>) => {
    const start = drag.current;
    if (!start) return;
    // Shift gives a fine drag; 0.25 BPM per pixel is the coarse default.
    const scale = event.shiftKey ? 0.05 : 0.25;
    const next = start.bpm - (event.clientY - start.y) * scale;
    onBpm(Math.round(Math.max(20, Math.min(300, next)) * 10) / 10);
  };

  const endBpmDrag = () => {
    drag.current = null;
  };

  const setBpm = (next: number) => onBpm(Math.round(Math.max(20, Math.min(300, next)) * 10) / 10);

  const onBpmKeyDown = (event: React.KeyboardEvent<HTMLDivElement>) => {
    const step = event.shiftKey ? 0.1 : 1;
    let next = transport.bpm;

    switch (event.key) {
      case "ArrowUp":
      case "ArrowRight":
        next += step;
        break;
      case "ArrowDown":
      case "ArrowLeft":
        next -= step;
        break;
      case "PageUp":
        next += 10;
        break;
      case "PageDown":
        next -= 10;
        break;
      case "Home":
        next = 20;
        break;
      case "End":
        next = 300;
        break;
      default:
        return;
    }

    event.preventDefault();
    setBpm(next);
  };

  return (
    <div className="ws-transport">
      <div className="ws-transport-group">
        <button
          className="ws-btn is-icon"
          onClick={onRewind}
          title="Return to bar 1 (Home)"
          aria-label="Return to bar 1"
        >
          ⏮
        </button>
        <button
          className="ws-btn is-icon"
          onClick={onStop}
          title="Stop and return to bar 1"
          aria-label="Stop"
        >
          ■
        </button>
        <button
          className="ws-btn is-icon is-play"
          aria-pressed={transport.playing}
          onClick={() => onPlay(!transport.playing)}
          title="Play or stop (Space)"
          aria-label={transport.playing ? "Pause" : "Play"}
        >
          {transport.playing ? "❚❚" : "▶"}
        </button>
        <button
          className="ws-btn is-icon is-rec"
          aria-pressed={recording}
          disabled={!onRecord}
          onClick={() => onRecord?.(!recording)}
          title={
            onRecord ? "Record armed tracks (punch at loop end)" : "Recording is not available"
          }
          aria-label="Record"
        >
          ●
        </button>
        <button
          className="ws-btn is-icon"
          aria-pressed={transport.loopEnabled}
          onClick={() => onLoop(!transport.loopEnabled)}
          title="Loop the arrange region"
          aria-label="Toggle loop"
        >
          ⟳
        </button>
        <button
          className="ws-btn is-icon"
          aria-pressed={transport.metronome}
          onClick={() => onMetronome(!transport.metronome)}
          title="Metronome click"
          aria-label="Toggle metronome"
        >
          ♩
        </button>
      </div>

      <div className="ws-sep" />

      <div className="ws-lcd ws-lcd-position" title="Musical position">
        <span className="ws-lcd-value" data-pos>
          {position.main}
        </span>
        <span className="ws-lcd-caption">{position.small}</span>
      </div>

      <div
        className="ws-lcd"
        style={{ cursor: "ns-resize", minWidth: 104 }}
        onPointerDown={beginBpmDrag}
        onPointerMove={moveBpmDrag}
        onPointerUp={endBpmDrag}
        onPointerCancel={endBpmDrag}
        onDoubleClick={() => setBpm(120)}
        onKeyDown={onBpmKeyDown}
        title="Drag or use arrow keys to change tempo (Shift for fine), double-click for 120"
        role="spinbutton"
        aria-label="Tempo"
        aria-valuenow={transport.bpm}
        aria-valuemin={20}
        aria-valuemax={300}
        aria-valuetext={`${transport.bpm.toFixed(1)} BPM`}
        tabIndex={0}
      >
        <span className="ws-lcd-value">{transport.bpm.toFixed(1)}</span>
        <span className="ws-lcd-caption">BPM / TEMPO</span>
      </div>

      {/* Committed time signature: the engine assumes 4/4 (beatsPerBar=4),
          so this is a fact readout, not an editable control. */}
      <div className="ws-lcd" title="Signature (4/4, fixed by the engine)">
        <span className="ws-lcd-value">4/4</span>
        <span className="ws-lcd-caption">4 BEATS</span>
      </div>

      {globalPitch && onGlobalPitch && (
        <div className="ws-transport-pitch" role="group" aria-label="Global pitch">
          <span className="ws-transport-pitch-label">GLOBAL</span>
          <PitchKnobs label="Global" value={globalPitch} onChange={onGlobalPitch} compact />
        </div>
      )}

      <div className="ws-meter ws-meter-fill-space" title="Master peak">
        <span className="ws-meter-label">MASTER {masterDb}</span>
        <span className="ws-meter-bar">
          <span
            className="ws-meter-fill"
            style={{ width: `${meterPct(transport.masterPeakLeft)}%` }}
          />
        </span>
        <span className="ws-meter-bar">
          <span
            className="ws-meter-fill"
            style={{ width: `${meterPct(transport.masterPeakRight)}%` }}
          />
        </span>
      </div>
    </div>
  );
}
