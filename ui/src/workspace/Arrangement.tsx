import { memo, useEffect, useMemo, useRef, useState } from "react";
import type { ChannelParams, PitchState, TimelineClip, TrackInfo, TransportState } from "../types";
import { barPct, tintForTrack, waveformPoints } from "./format";

export type ArrangeTool = "select" | "trim" | "fade" | "gain" | "split" | "erase";

// Pro Tools edit tools: 1 Grabber, 2 Trim, 3 Fade, 4 Volume, 5 X, 6 Erase.
// V/T/F/G/X/E kept as aliases.
const TOOLS: { id: ArrangeTool; label: string; key: string; hint: string }[] = [
  { id: "select", label: "Select", key: "1", hint: "Select clips; drag to move" },
  { id: "trim", label: "Trim", key: "2", hint: "Drag a clip edge to trim it" },
  { id: "fade", label: "Fade", key: "3", hint: "Drag the left or right half for a fade" },
  { id: "gain", label: "Gain", key: "4", hint: "Drag up/down to change clip gain" },
  { id: "split", label: "Split", key: "5", hint: "Click a clip to split it at the pointer" },
  { id: "erase", label: "Erase", key: "6", hint: "Click a clip to delete it" },
];

const TOOL_ALIASES: Record<string, ArrangeTool> = {
  v: "select",
  t: "trim",
  f: "fade",
  g: "gain",
  x: "split",
  e: "erase",
  "1": "select",
  "2": "trim",
  "3": "fade",
  "4": "gain",
  "5": "split",
  "6": "erase",
};

const FUNCTIONS = [
  { id: "split", label: "SPLIT", hint: "Split the selected clip at the playhead" },
  { id: "dup", label: "DUP", hint: "Duplicate the selected clip after itself" },
  { id: "del", label: "DEL", hint: "Delete the selected clip" },
] as const;

const MIN_BARS = 4;
const MAX_BARS = 256;

type DragMode = "move" | "trimLeft" | "trimRight" | "fadeIn" | "fadeOut" | "gain";

type DragState = {
  mode: DragMode;
  index: number;
  x: number;
  y: number;
  origin: TimelineClip;
  laneWidth: number;
};

const clamp = (value: number, low: number, high: number) => Math.max(low, Math.min(high, value));
const roundTo = (value: number, step: number) => Math.round(value / step) * step;

function slicePeaks(peaks: number[] | undefined, from: number, to: number): number[] | undefined {
  if (!peaks?.length) return peaks;
  const start = Math.floor(clamp(from, 0, 1) * peaks.length);
  const end = Math.max(start + 1, Math.ceil(clamp(to, 0, 1) * peaks.length));
  return peaks.slice(start, end);
}

const ClipView = memo(function ClipView({
  clip,
  selected,
  left,
  width,
  tool,
  onPointerDown,
  onPointerMove,
  onPointerUp,
  onOpen,
  onGain,
}: {
  clip: TimelineClip;
  selected: boolean;
  left: number;
  width: number;
  tool: ArrangeTool;
  onPointerDown: (event: React.PointerEvent<HTMLDivElement>) => void;
  onPointerMove: (event: React.PointerEvent<HTMLDivElement>) => void;
  onPointerUp: (event: React.PointerEvent<HTMLDivElement>) => void;
  onOpen: () => void;
  onGain?: (db: number) => void;
}) {
  const points = waveformPoints(clip.peaks ?? []);
  const tint = tintForTrack(clip.track);
  const fadeIn =
    clip.fadeInBars && clip.lengthBars > 0 ? (clip.fadeInBars / clip.lengthBars) * 100 : 0;
  const fadeOut =
    clip.fadeOutBars && clip.lengthBars > 0 ? (clip.fadeOutBars / clip.lengthBars) * 100 : 0;
  const compact = width < 3;
  const [faderOpen, setFaderOpen] = useState(false);

  return (
    <div
      className={`ws-clip tool-${tool}${selected ? " is-selected" : ""}`}
      role="group"
      tabIndex={0}
      data-selected={selected || undefined}
      aria-label={`${clip.name || "Clip"}, bar ${clip.startBar}, ${clip.lengthBars} bars${selected ? ", selected" : ""}`}
      onKeyDown={(event) => {
        // Outer group owns Enter/Space (open). The volume slider inside stops
        // its own keys, so the two never fight.
        if (event.key !== "Enter" && event.key !== " ") return;
        event.preventDefault();
        onOpen();
      }}
      style={
        {
          left: `${left}%`,
          width: `${Math.max(0.4, width)}%`,
          "--tint": tint,
        } as React.CSSProperties
      }
      onPointerDown={onPointerDown}
      onPointerMove={onPointerMove}
      onPointerUp={onPointerUp}
      onPointerCancel={onPointerUp}
      onDoubleClick={(event) => {
        event.stopPropagation();
        onOpen();
      }}
      title={`${clip.name || "Clip"} · bar ${clip.startBar} · ${clip.lengthBars} bars${
        tool === "erase" ? " — click to erase" : tool === "split" ? " — click to split" : ""
      }`}
    >
      <div className="ws-clip-head ws-clip-head-quiet">
        <span className="ws-clip-name">{compact ? "" : clip.name || "Clip"}</span>
      </div>

      <div className="ws-clip-body">
        {points && (
          <svg
            className="ws-clip-wave"
            viewBox="0 0 100 100"
            preserveAspectRatio="none"
            aria-hidden="true"
            focusable="false"
          >
            <polygon points={points} />
            {fadeIn > 0.5 && (
              <polyline className="ws-fade" points={`0,100 ${fadeIn.toFixed(2)},0`} />
            )}
            {fadeOut > 0.5 && (
              <polyline className="ws-fade" points={`100,100 ${(100 - fadeOut).toFixed(2)},0`} />
            )}
          </svg>
        )}

        {fadeIn > 0.5 && (
          <span className="ws-clip-fade is-in" style={{ width: `${fadeIn}%` }} aria-hidden="true" />
        )}
        {fadeOut > 0.5 && (
          <span
            className="ws-clip-fade is-out"
            style={{ width: `${fadeOut}%` }}
            aria-hidden="true"
          />
        )}

        {clip.notes && clip.notes.length > 0 && (
          <span className="ws-clip-notes" aria-hidden="true">
            {clip.notes.slice(0, 96).map((note, index) => {
              const span = Math.max(1, clip.lengthBars * 4);
              return (
                <i
                  key={`${note.pitch}-${note.startBeat}-${index}`}
                  style={{
                    left: `${(note.startBeat / span) * 100}%`,
                    width: `${Math.max(1, (note.lengthBeats / span) * 100)}%`,
                    bottom: `${clamp(((note.pitch - 36) / 48) * 100, 0, 94)}%`,
                  }}
                />
              );
            })}
          </span>
        )}

        <button
          className="ws-clip-vol-btn"
          type="button"
          aria-expanded={faderOpen}
          aria-label={`${clip.name || "Clip"} volume ${clip.gainDb.toFixed(1)} dB`}
          title={`Clip volume ${clip.gainDb.toFixed(1)} dB — click for fader`}
          onClick={(event) => {
            event.stopPropagation();
            setFaderOpen((value) => !value);
          }}
          onPointerDown={(event) => event.stopPropagation()}
        >
          ▢
        </button>
        {faderOpen && (
          <span
            className="ws-clip-fader-pop"
            onClick={(event) => event.stopPropagation()}
            onPointerDown={(event) => event.stopPropagation()}
          >
            <input
              className="ws-clip-fader-range"
              type="range"
              min={-24}
              max={12}
              step={0.1}
              value={clip.gainDb}
              aria-label={`${clip.name || "Clip"} volume`}
              onChange={(event) => onGain?.(Number(event.target.value))}
              style={{ "--v": (clip.gainDb + 24) / 36 } as React.CSSProperties}
            />
            <span className="ws-clip-fader-read">
              {clip.gainDb > 0 ? "+" : ""}
              {clip.gainDb.toFixed(1)} dB
            </span>
          </span>
        )}

        {!compact && <span className="ws-clip-meta">{clip.startBar}</span>}

        {(tool === "fade" || selected) && (
          <>
            <span className="ws-fade-handle is-in" aria-hidden="true" />
            <span className="ws-fade-handle is-out" aria-hidden="true" />
          </>
        )}
      </div>
    </div>
  );
});

export interface ArrangementProps {
  tracks: TrackInfo[];
  clips: TimelineClip[];
  selectedTrack: number;
  selectedClip: number | null;
  transport?: TransportState;
  inspectorTrack?: TrackInfo;
  onSelectTrack: (track: number) => void;
  onSelectClip: (clip: number | null) => void;
  onSeek: (bar: number) => void;
  onMute: (track: number, muted: boolean) => void;
  onSolo: (track: number, soloed: boolean) => void;
  onArm: (track: number, armed: boolean) => void;
  onGain: (track: number, db: number) => void;
  onPan: (track: number, pan: number) => void;
  onSendA: (track: number, amount: number) => void;
  onSendB: (track: number, amount: number) => void;
  onInsert: (track: number, slot: number) => void;
  onRack: (track: number) => void;
  onClipsChanged?: (clips: TimelineClip[]) => void;
  onEditClip?: (index: number, patch: Record<string, unknown>) => void;
  onSplitClip?: (index: number, bar: number) => void;
  onDuplicateClip?: (index: number) => void;
  onDeleteClip?: (index: number) => void;
  onLoopRange?: (startBar: number, endBar: number) => void;
  undoDepth?: number;
  redoDepth?: number;
  onUndo?: () => void;
  onRedo?: () => void;
  channel?: ChannelParams;
  onChannelParam?: (id: string, value: number) => void;
  onOpenClip?: (clip: number) => void;
  pitch?: PitchState;
  onPitch?: (patch: Partial<PitchState>) => void;
  instrumentPitch?: PitchState;
  onInstrumentPitch?: (patch: Partial<PitchState>) => void;
  onRegisterFit?: (fit: () => void) => void;
  defaultTrackHeight?: number;
  defaultFadeBars?: number;
  defaultClipGain?: number;
  defaultSnap?: boolean;
}

export function Arrangement({
  tracks,
  clips: sourceClips,
  selectedTrack,
  selectedClip,
  transport,
  onSelectTrack,
  onSelectClip,
  onSeek,
  onMute,
  onSolo,
  onArm,
  onRack,
  onClipsChanged,
  onEditClip,
  onSplitClip,
  onDuplicateClip,
  onDeleteClip,
  onLoopRange,
  undoDepth = 0,
  redoDepth = 0,
  onUndo,
  onRedo,
  onOpenClip,
  onRegisterFit,
  defaultTrackHeight = 84,
  defaultFadeBars = 0,
  defaultClipGain = 0,
  defaultSnap = true,
}: ArrangementProps) {
  const [visibleBars, setVisibleBars] = useState(32);
  const [viewStart, setViewStart] = useState(1);
  const [snap, setSnap] = useState(defaultSnap);
  const [tool, setTool] = useState<ArrangeTool>("select");
  const [trackHeights, setTrackHeights] = useState<Record<number, number>>({});
  const [foldedTracks, setFoldedTracks] = useState<Record<number, boolean>>({});
  const [loopStart, setLoopStart] = useState(() => transport?.loopStartBar ?? 1);
  const [loopEnd, setLoopEnd] = useState(() => transport?.loopEndBar ?? 17);
  const [draft, setDraft] = useState<TimelineClip[] | null>(null);
  const drag = useRef<DragState | null>(null);
  const arrangeRef = useRef<HTMLDivElement>(null);
  const pendingEdit = useRef<{ index: number; patch: Record<string, unknown> } | null>(null);
  const loopDrag = useRef<{
    mode: "start" | "end" | "move";
    x: number;
    start: number;
    end: number;
  } | null>(null);
  const heightDragCleanup = useRef<(() => void) | null>(null);

  // A header resize owns window listeners until pointer up; unmounting mid-drag
  // must not leave them pointing at a dead component.
  useEffect(() => () => heightDragCleanup.current?.(), []);

  useEffect(() => {
    if (transport?.loopStartBar !== undefined) setLoopStart(transport.loopStartBar);
    if (transport?.loopEndBar !== undefined) setLoopEnd(transport.loopEndBar);
  }, [transport?.loopStartBar, transport?.loopEndBar]);

  // Host truth wins once it arrives; the optimistic copy exists only so a
  // gesture does not wait for the IPC round-trip.
  useEffect(() => {
    setDraft(null);
  }, [sourceClips]);

  const clips = draft ?? sourceClips;

  // FIT lives in the header (by DSP) so it never scrolls away.
  useEffect(() => {
    onRegisterFit?.(() => {
      const sessionEnd = Math.max(17, ...clips.map((clip) => clip.startBar + clip.lengthBars));
      setViewStart(1);
      setVisibleBars(clamp(sessionEnd - 1, MIN_BARS, MAX_BARS));
    });
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [clips.length]);

  const commit = (next: TimelineClip[]) => {
    setDraft(next);
    onClipsChanged?.(next);
  };

  const seek = (bar: number) => onSeek(snap ? Math.round(bar) : bar);
  const playheadPct = transport ? barPct(transport.positionBar, viewStart, visibleBars) : -1;

  // Track height: 30 px folded, 84 px by default, drag the header edge to
  // change it. Height is written inline because the head and its lane are
  // separate grid children of the same row.
  const heightFor = (index: number) =>
    foldedTracks[index] ? 30 : (trackHeights[index] ?? defaultTrackHeight);

  const beginHeightDrag = (event: React.PointerEvent<HTMLSpanElement>, index: number) => {
    event.stopPropagation();
    event.preventDefault();
    heightDragCleanup.current?.();
    const startY = event.clientY;
    const startHeight = heightFor(index);
    const move = (moveEvent: PointerEvent) => {
      const next = clamp(Math.round(startHeight + (moveEvent.clientY - startY)), 30, 240);
      setTrackHeights((current) => ({ ...current, [index]: next }));
      setFoldedTracks((current) => ({ ...current, [index]: next <= 30 }));
    };
    const end = () => {
      window.removeEventListener("pointermove", move);
      window.removeEventListener("pointerup", end);
      window.removeEventListener("pointercancel", end);
      heightDragCleanup.current = null;
    };
    heightDragCleanup.current = end;
    window.addEventListener("pointermove", move);
    window.addEventListener("pointerup", end);
    window.addEventListener("pointercancel", end);
  };

  const barAt = (clientX: number, element: Element | null): number => {
    const box = element?.getBoundingClientRect();
    // Synthetic pointer events can omit clientX; never let that become NaN.
    if (!box || box.width <= 0 || !Number.isFinite(clientX)) return viewStart;
    const ratio = (clientX - box.left) / box.width;
    return viewStart + clamp(ratio, 0, 1) * visibleBars;
  };

  const laneBar = (clientX: number, element: Element | null) => {
    const bar = barAt(clientX, element);
    return snap ? Math.round(bar) : bar;
  };

  // Pro Tools-style stepped zoom: the +/- buttons and keys pivot on the
  // playhead the way the wheel pivots on the pointer. A playhead outside
  // the window clamps to the nearest edge instead of warping the view.
  const zoomAt = (direction: "in" | "out") => {
    const ratio = transport
      ? clamp((transport.positionBar - viewStart) / visibleBars, 0, 1)
      : 0.5;
    const next = zoomWindow(viewStart, visibleBars, ratio, direction, MIN_BARS, MAX_BARS);
    setVisibleBars(next.visibleBars);
    setViewStart(next.viewStart);
  };

  // Pro Tools wheel: every wheel event zooms at the pointer (Ctrl/Cmd too).
  // The anchor comes from the lane itself, not a guessed header width, so the
  // bar under the cursor stays put and the in/out factors are reciprocal.
  useEffect(() => {
    const el = arrangeRef.current;
    if (!el) return;
    const onNativeWheel = (event: WheelEvent) => {
      const target = event.target as HTMLElement;
      if (target.closest(".ws-head")) return;
      if (event.shiftKey) {
        event.preventDefault();
        el.scrollTop += event.deltaY;
        return;
      }
      const rect = el.getBoundingClientRect();
      if (rect.width <= 0) return;
      event.preventDefault();
      const ratio = (event.clientX - rect.left - 272) / Math.max(1, rect.width - 272);
      const anchorBar = viewStart + clamp(ratio, 0, 1) * visibleBars;
      const factor = event.deltaY < 0 ? 0.9 : 1.11;
      const next = clamp(visibleBars * factor, MIN_BARS, MAX_BARS);
      const anchorRatio = (anchorBar - viewStart) / visibleBars;
      setVisibleBars(next);
      setViewStart(Math.max(1, anchorBar - anchorRatio * next));
    };
    el.addEventListener("wheel", onNativeWheel, { passive: false });
    return () => el.removeEventListener("wheel", onNativeWheel);
  }, [viewStart, visibleBars]);

  // Pro Tools edit tools: 1–6 (+ V/T/F/G/X/E aliases), Cmd/Ctrl+Z undo,
  // Cmd/Ctrl+Shift+Z + Ctrl+Y redo, +/- zoom, L loop.
  useEffect(() => {
    const onKey = (event: KeyboardEvent) => {
      const target = event.target as HTMLElement | null;
      if (target && (/^(INPUT|TEXTAREA|SELECT)$/.test(target.tagName) || target.isContentEditable))
        return;
      const mod = event.ctrlKey || event.metaKey;
      const key = event.key.toLowerCase();
      if (mod && key === "z") {
        event.preventDefault();
        if (event.shiftKey) onRedo?.();
        else onUndo?.();
        return;
      }
      if (mod && key === "y") {
        event.preventDefault();
        onRedo?.();
        return;
      }
      if (mod) return;
      const aliased = TOOL_ALIASES[key];
      if (aliased) {
        event.preventDefault();
        setTool(aliased);
        return;
      }
      if (event.key === "-") {
        event.preventDefault();
        zoomAt("out");
        return;
      }
      if (event.key === "+" || event.key === "=") {
        event.preventDefault();
        zoomAt("in");
        return;
      }
      if (event.key === "Escape") setTool("select");
      if ((event.key === "Delete" || event.key === "Backspace") && selectedClip !== null) {
        event.preventDefault();
        onDeleteClip?.(selectedClip);
        commit(clips.filter((_, index) => index !== selectedClip));
        onSelectClip(null);
      }
    };
    window.addEventListener("keydown", onKey);
    return () => window.removeEventListener("keydown", onKey);
  }, [clips, selectedClip, onSelectClip, onDeleteClip, onUndo, onRedo, viewStart, visibleBars, transport?.positionBar]);

  const deleteClip = (index: number) => {
    onDeleteClip?.(index);
    commit(clips.filter((_, entry) => entry !== index));
    if (selectedClip === index) onSelectClip(null);
  };

  const duplicateClip = (index: number) => {
    const clip = clips[index];
    if (!clip) return;
    const copy: TimelineClip = {
      ...clip,
      name: `${clip.name} COPY`,
      startBar: clip.startBar + clip.lengthBars,
      gainDb: defaultClipGain,
      fadeInBars: defaultFadeBars,
      fadeOutBars: defaultFadeBars,
      peaks: clip.peaks ? [...clip.peaks] : undefined,
      notes: clip.notes ? clip.notes.map((note) => ({ ...note })) : undefined,
    };
    onDuplicateClip?.(index);
    const next = [...clips];
    next.splice(index + 1, 0, copy);
    commit(next);
    onSelectClip(index + 1);
  };

  const splitClipAt = (index: number, bar: number) => {
    const clip = clips[index];
    if (!clip || !Number.isFinite(bar)) return;
    const split = clamp(Math.round(bar), clip.startBar + 1, clip.startBar + clip.lengthBars - 1);
    if (split <= clip.startBar || split >= clip.startBar + clip.lengthBars) return;

    const leftBars = split - clip.startBar;
    const rightBars = clip.lengthBars - leftBars;
    const leftNotes = clip.notes
      ?.filter((note) => note.startBeat < leftBars * 4)
      .map((note) => ({
        ...note,
        lengthBeats: Math.min(note.lengthBeats, leftBars * 4 - note.startBeat),
      }));
    const rightNotes = clip.notes
      ?.filter((note) => note.startBeat + note.lengthBeats > leftBars * 4)
      .map((note) => ({
        ...note,
        startBeat: Math.max(0, note.startBeat - leftBars * 4),
        lengthBeats: note.lengthBeats - Math.max(0, leftBars * 4 - note.startBeat),
      }));

    const left: TimelineClip = {
      ...clip,
      lengthBars: leftBars,
      peaks: slicePeaks(clip.peaks, 0, leftBars / clip.lengthBars),
      notes: leftNotes,
      fadeOutBars: 0,
    };
    const right: TimelineClip = {
      ...clip,
      startBar: split,
      lengthBars: rightBars,
      name: `${clip.name} B`,
      gainDb: defaultClipGain,
      peaks: slicePeaks(clip.peaks, leftBars / clip.lengthBars, 1),
      notes: rightNotes,
      fadeInBars: defaultFadeBars,
    };

    onSplitClip?.(index, split);
    const next = [...clips];
    next.splice(index, 1, left, right);
    commit(next);
    onSelectClip(index + 1);
  };

  const splitSelectedAtPlayhead = () => {
    if (selectedClip === null || !transport) return;
    splitClipAt(selectedClip, transport.positionBar);
  };

  const startClipGesture = (event: React.PointerEvent<HTMLDivElement>, index: number) => {
    event.stopPropagation();
    const clip = clips[index];
    if (!clip) return;
    onSelectTrack(clip.track);
    onSelectClip(index);

    if (tool === "erase") {
      deleteClip(index);
      return;
    }
    if (tool === "split") {
      splitClipAt(index, laneBar(event.clientX, event.currentTarget.parentElement));
      return;
    }

    const box = event.currentTarget.getBoundingClientRect();
    const ratio = box.width > 0 ? (event.clientX - box.left) / box.width : 0.5;
    let mode: DragMode = "move";
    if (tool === "trim") mode = ratio < 0.5 ? "trimLeft" : "trimRight";
    else if (tool === "fade") mode = ratio < 0.5 ? "fadeIn" : "fadeOut";
    else if (tool === "gain") mode = "gain";

    // Some pointer streams (and pointercancel in jsdom) omit coordinates;
    // never start a drag that would compute a NaN edit.
    if (!Number.isFinite(event.clientX) || !Number.isFinite(event.clientY)) return;

    const lane = event.currentTarget.parentElement;
    drag.current = {
      mode,
      index,
      x: event.clientX,
      y: event.clientY,
      origin: clip,
      laneWidth: lane?.getBoundingClientRect().width ?? box.width,
    };
    event.currentTarget.setPointerCapture?.(event.pointerId);
  };

  const moveClipGesture = (event: React.PointerEvent<HTMLDivElement>) => {
    const state = drag.current;
    if (!state) return;
    if (!Number.isFinite(event.clientX) || !Number.isFinite(event.clientY)) return;
    const deltaBars = ((event.clientX - state.x) / Math.max(1, state.laneWidth)) * visibleBars;
    const origin = state.origin;
    const clip: TimelineClip = { ...origin, peaks: origin.peaks ? [...origin.peaks] : undefined };

    switch (state.mode) {
      case "move": {
        clip.startBar = Math.max(1, Math.round(origin.startBar + deltaBars));
        break;
      }
      case "trimLeft": {
        const start = clamp(
          Math.round(origin.startBar + deltaBars),
          1,
          origin.startBar + origin.lengthBars - 1,
        );
        const ratio = (start - origin.startBar) / origin.lengthBars;
        clip.startBar = start;
        clip.lengthBars = origin.lengthBars - (start - origin.startBar);
        clip.peaks = slicePeaks(origin.peaks, ratio, 1);
        break;
      }
      case "trimRight": {
        clip.lengthBars = clamp(Math.round(origin.lengthBars + deltaBars), 1, MAX_BARS);
        clip.peaks = slicePeaks(origin.peaks, 0, clip.lengthBars / origin.lengthBars);
        break;
      }
      case "fadeIn": {
        clip.fadeInBars = clamp(
          roundTo((origin.fadeInBars ?? 0) + deltaBars, 0.25),
          0,
          origin.lengthBars,
        );
        break;
      }
      case "fadeOut": {
        clip.fadeOutBars = clamp(
          roundTo((origin.fadeOutBars ?? 0) - deltaBars, 0.25),
          0,
          origin.lengthBars,
        );
        break;
      }
      case "gain": {
        clip.gainDb = roundTo(
          clamp(origin.gainDb - (event.clientY - state.y) * 0.25, -24, 12),
          0.1,
        );
        break;
      }
    }

    pendingEdit.current = {
      index: state.index,
      patch: {
        startBar: clip.startBar,
        lengthBars: clip.lengthBars,
        gainDb: clip.gainDb,
        fadeInBars: clip.fadeInBars ?? 0,
        fadeOutBars: clip.fadeOutBars ?? 0,
        trimmed: state.mode === "trimLeft" || state.mode === "trimRight",
      },
    };

    const next = [...clips];
    next[state.index] = clip;
    commit(next);
  };

  const endClipGesture = () => {
    if (pendingEdit.current) {
      onEditClip?.(pendingEdit.current.index, pendingEdit.current.patch);
      pendingEdit.current = null;
    }
    drag.current = null;
  };

  const beginLoopDrag = (event: React.PointerEvent<HTMLDivElement>) => {
    event.stopPropagation();
    const box = event.currentTarget.getBoundingClientRect();
    if (box.width <= 0) return;
    const xOf = (bar: number) => box.left + ((bar - viewStart) / visibleBars) * box.width;
    const startX = xOf(loopStart);
    const endX = xOf(loopEnd);
    const bar = barAt(event.clientX, event.currentTarget);

    if (Math.abs(event.clientX - startX) <= 8) {
      loopDrag.current = { mode: "start", x: event.clientX, start: loopStart, end: loopEnd };
    } else if (Math.abs(event.clientX - endX) <= 8) {
      loopDrag.current = { mode: "end", x: event.clientX, start: loopStart, end: loopEnd };
    } else if (event.clientX > startX && event.clientX < endX) {
      loopDrag.current = { mode: "move", x: event.clientX, start: loopStart, end: loopEnd };
    } else {
      seek(bar);
      return;
    }
    event.currentTarget.setPointerCapture?.(event.pointerId);
  };

  const moveLoopDrag = (event: React.PointerEvent<HTMLDivElement>) => {
    const state = loopDrag.current;
    if (!state) return;
    if (!Number.isFinite(event.clientX)) return;
    const box = event.currentTarget.getBoundingClientRect();
    if (box.width <= 0) return;
    const delta = Math.round(((event.clientX - state.x) / box.width) * visibleBars);

    if (state.mode === "start") {
      setLoopStart(clamp(state.start + delta, 1, state.end - 1));
    } else if (state.mode === "end") {
      setLoopEnd(clamp(state.end + delta, state.start + 1, MAX_BARS));
    } else {
      const length = state.end - state.start;
      const start = Math.max(1, state.start + delta);
      setLoopStart(start);
      setLoopEnd(start + length);
    }
  };

  const endLoopDrag = () => {
    if (loopDrag.current) onLoopRange?.(loopStart, loopEnd);
    loopDrag.current = null;
  };

  const onLoopKeyDown = (event: React.KeyboardEvent<HTMLDivElement>) => {
    if (event.key !== "ArrowLeft" && event.key !== "ArrowRight") return;
    event.preventDefault();

    const shift = event.shiftKey ? 4 : 1;
    const direction = event.key === "ArrowRight" ? shift : -shift;
    let nextStart = loopStart;
    let nextEnd = loopEnd;

    if (event.altKey) {
      nextStart = clamp(loopStart + direction, 1, loopEnd - 1);
    } else if (event.ctrlKey || event.metaKey) {
      nextEnd = clamp(loopEnd + direction, loopStart + 1, MAX_BARS);
    } else {
      const length = loopEnd - loopStart;
      nextStart = Math.max(1, loopStart + direction);
      nextEnd = nextStart + length;
      if (nextEnd > MAX_BARS) {
        nextEnd = MAX_BARS;
        nextStart = nextEnd - length;
      }
    }

    setLoopStart(nextStart);
    setLoopEnd(nextEnd);
    onLoopRange?.(nextStart, nextEnd);
  };

  /* Bar lines always draw; number labels thin out as the window zooms out
     so ~18px labels never overprint at ~12px pitch (256 bars). */
  const labelStride = visibleBars > 128 ? 8 : visibleBars > 64 ? 4 : visibleBars > 16 ? 2 : 1;
  const ticks = useMemo(() => {
    const first = Math.floor(viewStart);
    const last = Math.ceil(viewStart + visibleBars);
    const rows: { bar: number; left: number; major: boolean; labelled: boolean }[] = [];
    for (let bar = first; bar <= last; ++bar) {
      const left = barPct(bar, viewStart, visibleBars);
      // Cull: off-screen ticks never render (clips already cull this way).
      if (left < -2 || left > 102) continue;
      const major = (bar - 1) % 4 === 0;
      rows.push({ bar, left, major, labelled: major && (bar - 1) % (4 * labelStride) === 0 });
    }
    return rows;
  }, [viewStart, visibleBars, labelStride]);

  // Group once per clip change — was O(tracks × clips) filter per render,
  // which re-ran on every 30fps transport tick.
  const clipsByTrack = useMemo(() => {
    const map = new Map<number, { clip: TimelineClip; index: number }[]>();
    clips.forEach((clip, index) => {
      const list = map.get(clip.track) ?? [];
      list.push({ clip, index });
      map.set(clip.track, list);
    });
    return map;
  }, [clips]);

  const hasSelection = selectedClip !== null;
  const loopLeft = barPct(loopStart, viewStart, visibleBars);
  const loopWidth = barPct(loopEnd, viewStart, visibleBars) - loopLeft;

  if (tracks.length === 0) {
    return <div className="ws-empty">No tracks in this session</div>;
  }

  return (
    <div className="ws-arrange-wrap">
      <div className={`ws-arrange tool-${tool}`} ref={arrangeRef}>
        <div className="ws-toolbar">
          <div className="ws-toolcluster" role="group" aria-label="Edit tools">
            {TOOLS.map((entry) => (
              <button
                key={entry.id}
                className="ws-btn ws-tool"
                aria-pressed={tool === entry.id}
                aria-label={`${entry.label} tool (${entry.key})`}
                title={`${entry.label} (${entry.key}) — ${entry.hint}`}
                onClick={() => setTool(entry.id)}
              >
                <span className="ws-tool-name">{entry.label.toUpperCase()}</span>
                <span className="ws-tool-key">{entry.key}</span>
              </button>
            ))}
          </div>

          <div className="ws-fncluster" role="group" aria-label="Clip functions">
            {FUNCTIONS.map((fn) => (
              <button
                key={fn.id}
                className="ws-btn ws-fn"
                disabled={!hasSelection}
                title={fn.hint}
                onClick={() => {
                  if (!hasSelection) return;
                  if (fn.id === "split") splitSelectedAtPlayhead();
                  else if (fn.id === "dup") duplicateClip(selectedClip as number);
                  else deleteClip(selectedClip as number);
                }}
              >
                {fn.label}
              </button>
            ))}
          </div>

          <div className="ws-fncluster" role="group" aria-label="History">
            <button
              className="ws-btn ws-fn"
              aria-label="Undo"
              title={`Undo the last clip edit (${undoDepth} available)`}
              disabled={undoDepth <= 0}
              onClick={onUndo}
            >
              UNDO
            </button>
            <button
              className="ws-btn ws-fn"
              aria-label="Redo"
              title={`Redo the last undone clip edit (${redoDepth} available)`}
              disabled={redoDepth <= 0}
              onClick={onRedo}
            >
              REDO
            </button>
          </div>

          <button
            className="ws-btn ws-chip"
            aria-pressed={snap}
            onClick={() => setSnap(!snap)}
            title={
              snap ? "Snap is on: clips and seeks land on bars" : "Snap is off: free positioning"
            }
          >
            <b>SNAP</b>
          </button>

          <span className="ws-spacer" />

          <button
            className="ws-btn is-icon"
            onClick={() => zoomAt("out")}
            aria-label="Zoom out"
            title="Zoom out"
          >
            −
          </button>
          <span className="ws-zoom-read" title="Timeline zoom">
            <i>VIEW</i>
            <b>{Math.round(visibleBars)} BARS</b>
          </span>
          <button
            className="ws-btn is-icon"
            onClick={() => zoomAt("in")}
            aria-label="Zoom in"
            title="Zoom in"
          >
            +
          </button>
        </div>

        <div className="ws-corner">
          <span>{tracks.length} TRACKS</span>
        </div>

        <div
          className="ws-ruler"
          onPointerDown={(event) => {
            if ((event.target as HTMLElement).closest(".ws-loopbar")) return;
            seek(barAt(event.clientX, event.currentTarget));
          }}
          title="Click to move the playhead"
        >
          <div
            className={`ws-loopbar${transport?.loopEnabled ? "" : " is-off"}`}
            role="slider"
            aria-label="Loop region"
            aria-valuemin={1}
            aria-valuemax={MAX_BARS}
            aria-valuenow={loopStart}
            aria-valuetext={`Bars ${loopStart} to ${loopEnd}`}
            tabIndex={0}
            title={
              transport?.loopEnabled
                ? "Drag the loop region; drag its ends to resize; arrow keys nudge it"
                : "Loop is off — enable it in the transport to hear it"
            }
            onKeyDown={onLoopKeyDown}
            onPointerDown={beginLoopDrag}
            onPointerMove={moveLoopDrag}
            onPointerUp={endLoopDrag}
            onPointerCancel={endLoopDrag}
          >
            <span
              className="ws-loop-range"
              style={{ left: `${loopLeft}%`, width: `${Math.max(0.5, loopWidth)}%` }}
            >
              <i className="ws-loop-handle is-start" />
              <i className="ws-loop-handle is-end" />
            </span>
          </div>

          <div className="ws-ticks">
            {ticks.map((tick) => (
              <span
                key={tick.bar}
                className={tick.major ? "ws-tick is-bar" : "ws-tick"}
                style={{ left: `${tick.left}%` }}
              >
                {tick.labelled && <span className="ws-tick-label">{tick.bar}</span>}
              </span>
            ))}
          </div>
          {playheadPct >= 0 && playheadPct <= 100 && (
            <span className="ws-playhead" style={{ left: `${playheadPct}%` }} />
          )}
        </div>

        {tracks.map((track) => {
          const trackClips = clipsByTrack.get(track.index) ?? [];
          const peak = transport?.trackPeaks[track.index] ?? 0;

          return (
            <div key={track.index} style={{ display: "contents" }}>
              <div
                className={foldedTracks[track.index] ? "ws-head is-folded" : "ws-head"}
                style={
                  {
                    height: heightFor(track.index),
                    "--tint": tintForTrack(track.index),
                  } as React.CSSProperties
                }
                aria-current={track.index === selectedTrack ? "true" : undefined}
                onClick={() => onSelectTrack(track.index)}
              >
                <span className="ws-tint" style={{ background: tintForTrack(track.index) }} />
                <span className="ws-head-compact" aria-hidden="true">
                  {String(track.index + 1).padStart(2, "0")}
                </span>
                <div className="ws-head-main">
                  <div className="ws-head-row">
                    <button
                      className="ws-mini-btn ws-head-fold"
                      aria-pressed={Boolean(foldedTracks[track.index])}
                      aria-label={`${foldedTracks[track.index] ? "Expand" : "Collapse"} ${track.name}`}
                      title={
                        foldedTracks[track.index] ? "Expand this track" : "Collapse this track"
                      }
                      onClick={(event) => {
                        event.stopPropagation();
                        setFoldedTracks((current) => ({
                          ...current,
                          [track.index]: !current[track.index],
                        }));
                      }}
                    >
                      {foldedTracks[track.index] ? "▸" : "▾"}
                    </button>
                    <span className="ws-head-name" title={track.name}>
                      {track.name}
                  </div>

                  <div className="ws-head-row">
                    </span>
                    <button
                      className="ws-mini-btn"
                      aria-pressed={track.muted}
                      onClick={(event) => {
                        event.stopPropagation();
                        onMute(track.index, !track.muted);
                      }}
                      title={`Mute ${track.name}`}
                      aria-label={`Mute ${track.name}`}
                    >
                      M
                    </button>
                    <button
                      className="ws-mini-btn is-solo"
                      aria-pressed={track.soloed}
                      onClick={(event) => {
                        event.stopPropagation();
                        onSolo(track.index, !track.soloed);
                      }}
                      title={`Solo ${track.name}`}
                      aria-label={`Solo ${track.name}`}
                    >
                      S
                    </button>
                    <button
                      className="ws-mini-btn is-rec"
                      aria-pressed={track.armed ?? false}
                      onClick={(event) => {
                        event.stopPropagation();
                        onArm(track.index, !track.armed);
                      }}
                      title={`Record-arm ${track.name}`}
                      aria-label={`Arm ${track.name}`}
                    >
                      R
                    </button>
                    <span
                      className="ws-head-db"
                      title={`T${track.index + 1} · ${track.gainDb.toFixed(1)} dB`}
                    >
                      {track.gainDb.toFixed(1)} dB
                    </span>
                    <span className="ws-head-meter" title={`Peak ${Math.round(peak * 100)}%`}>
                      <i style={{ width: `${Math.min(100, peak * 100)}%` }} />
                    </span>
                  </div>
                </div>

                <button
                  className="ws-head-rack"
                  onClick={(event) => {
                    event.stopPropagation();
                    onRack(track.index);
                  }}
                  title={`Open the channel DSP rack for TRACK ${track.index + 1}`}
                  aria-label={`Rack ${track.name}`}
                >
                  DSP
                </button>

                <span
                  className="ws-head-resize"
                  role="separator"
                  aria-label={`Resize ${track.name}`}
                  aria-orientation="horizontal"
                  tabIndex={0}
                  title="Drag to resize this track; double-click for the default height. Arrow keys resize when focused."
                  onPointerDown={(event) => beginHeightDrag(event, track.index)}
                  onDoubleClick={(event) => {
                    event.stopPropagation();
                    setTrackHeights((current) => ({ ...current, [track.index]: 84 }));
                    setFoldedTracks((current) => ({ ...current, [track.index]: false }));
                  }}
                  onKeyDown={(event) => {
                    if (event.key !== "ArrowUp" && event.key !== "ArrowDown") return;
                    event.preventDefault();
                    event.stopPropagation();
                    const delta = event.key === "ArrowUp" ? 4 : -4;
                    setTrackHeights((current) => {
                      const next = clamp(Math.round((current[track.index] ?? 84) + delta), 30, 240);
                      setFoldedTracks((folded) => ({ ...folded, [track.index]: next <= 30 }));
                      return { ...current, [track.index]: next };
                    });
                  }}
                />
              </div>

              <div
                className={[
                  "ws-lane",
                  `tool-${tool}`,
                  track.index === selectedTrack ? "is-selected" : "",
                  foldedTracks[track.index] ? "is-folded" : "",
                ]
                  .filter(Boolean)
                  .join(" ")}
                style={
                  {
                    height: heightFor(track.index),
                    "--tint": tintForTrack(track.index),
                  } as React.CSSProperties
                }
                onPointerDown={(event) => {
                  onSelectTrack(track.index);
                  onSelectClip(null);
                  seek(barAt(event.clientX, event.currentTarget));
                }}
                title={`TRACK ${track.index + 1} — click to place the playhead`}
              >
                {trackClips.map(({ clip, index }) => {
                  const left = barPct(clip.startBar, viewStart, visibleBars);
                  const width =
                    barPct(clip.startBar + clip.lengthBars, viewStart, visibleBars) - left;
                  if (left > 100 || left + width < 0) return null;

                  return (
                    <ClipView
                      key={`${clip.track}-${index}`}
                      clip={clip}
                      selected={selectedClip === index}
                      left={left}
                      width={width}
                      tool={tool}
                      onPointerDown={(event) => startClipGesture(event, index)}
                      onPointerMove={moveClipGesture}
                      onPointerUp={endClipGesture}
                      onOpen={() => {
                        onSelectClip(index);
                        onOpenClip?.(index);
                      }}
                      onGain={(db) => {
                        const next = [...clips];
                        next[index] = { ...clip, gainDb: db };
                        commit(next);
                        onEditClip?.(index, { gainDb: db });
                      }}
                    />
                  );
                })}

                {playheadPct >= 0 && playheadPct <= 100 && (
                  <span className="ws-playhead" style={{ left: `${playheadPct}%` }} />
                )}
              </div>
            </div>
          );
        })}

        <div className="ws-hint">
          {TOOLS.find((entry) => entry.id === tool)?.hint.toUpperCase()} · SELECT A CLIP TO TRIM ITS
          VOLUME · WHEEL ZOOMS · SHIFT+WHEEL SCROLLS
        </div>
      </div>
    </div>
  );
}
