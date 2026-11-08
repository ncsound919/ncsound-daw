import { useEffect, useRef, useState } from "react";
import type { MidiNote } from "../types";

/**
 * FL-style note editor for one MIDI clip. Draw adds a note and selects it,
 * Paint keeps adding as the pointer moves, Erase paints notes away, and a
 * right-click removes a note in any tool. Notes drag to move, their right edge
 * resizes, and the velocity lane drags per note. The grid is 48 semitones
 * (C2..C6) and snaps to the selected division.
 */
const PITCH_LOW = 36;
const PITCH_HIGH = 84;
const ROW_H = 18;

const NOTE_NAMES = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"];

const SNAPS = [
  { label: "1/4", beats: 1 },
  { label: "1/8", beats: 0.5 },
  { label: "1/16", beats: 0.25 },
  { label: "1/32", beats: 0.125 },
];

type Tool = "draw" | "paint" | "erase";

const TOOLS: { id: Tool; label: string; short: string; hint: string }[] = [
  { id: "draw", label: "Draw tool", short: "DRAW", hint: "Click adds a note" },
  { id: "paint", label: "Paint tool", short: "PAINT", hint: "Drag to paint a run of notes" },
  { id: "erase", label: "Erase tool", short: "ERASE", hint: "Click or drag to remove notes" },
];

function noteName(pitch: number): string {
  return `${NOTE_NAMES[((pitch % 12) + 12) % 12]}${Math.floor(pitch / 12) - 1}`;
}

function isBlack(pitch: number): boolean {
  return NOTE_NAMES[((pitch % 12) + 12) % 12].includes("#");
}

/** Pointer coordinates are absent on synthetic events; treat them as the origin. */
function num(value: number | undefined): number {
  return Number.isFinite(value) ? (value as number) : 0;
}

const clamp = (value: number, min: number, max: number) => Math.max(min, Math.min(max, value));

export interface PianoRollProps {
  clipName: string;
  lengthBeats: number;
  initialNotes?: MidiNote[];
  persisted?: boolean;
  onEdited?: (notes: MidiNote[]) => void;
}

type Drag = {
  mode: "move" | "resize" | "velocity";
  index: number;
  startX: number;
  startY: number;
  origin: MidiNote;
};

export function PianoRoll({
  clipName,
  lengthBeats,
  initialNotes = [],
  persisted = false,
  onEdited,
}: PianoRollProps) {
  const [notes, setNotes] = useState<MidiNote[]>(initialNotes);
  const [selected, setSelected] = useState<number | null>(null);
  const [beatPx, setBeatPx] = useState(44);
  const [snap, setSnap] = useState(0.25);
  const [tool, setTool] = useState<Tool>("draw");
  const rootRef = useRef<HTMLDivElement | null>(null);
  const gridRef = useRef<HTMLDivElement | null>(null);
  const drag = useRef<Drag | null>(null);
  const painting = useRef(false);
  const notesRef = useRef<MidiNote[]>(initialNotes);
  const pendingNotes = useRef<MidiNote[] | null>(null);

  const gridW = Math.max(120, lengthBeats * beatPx);
  const gridH = (PITCH_HIGH - PITCH_LOW + 1) * ROW_H;

  // Host truth resyncs when the clip changes under the editor (undo, split) or
  // when it echoes an edit. A gesture in flight owns the surface until it ends.
  useEffect(() => {
    if (drag.current || painting.current) return;
    if (initialNotes === notesRef.current) return;
    notesRef.current = initialNotes;
    setNotes(initialNotes);
  }, [initialNotes]);

  const setLocal = (next: MidiNote[]) => {
    notesRef.current = next;
    setNotes(next);
  };

  // Discrete edits (add/remove) commit once. A drag or paint gesture stays
  // local and emits a single write on pointer up, so the host never sees an
  // undo step per pixel of movement.
  const commit = (next: MidiNote[]) => {
    pendingNotes.current = null;
    setLocal(next);
    onEdited?.(next);
  };

  const defer = (next: MidiNote[]) => {
    pendingNotes.current = next;
    setLocal(next);
  };

  const flushPending = () => {
    const next = pendingNotes.current;
    if (next === null) return;
    pendingNotes.current = null;
    onEdited?.(next);
  };

  const pointFrom = (event: React.PointerEvent) => {
    const box = gridRef.current?.getBoundingClientRect();
    const x = num(event.clientX) - (box?.left ?? 0);
    const y = num(event.clientY) - (box?.top ?? 0);
    return { x: clamp(x, 0, gridW), y: clamp(y, 0, gridH) };
  };

  const pitchAt = (y: number) => clamp(PITCH_HIGH - Math.floor(y / ROW_H), PITCH_LOW, PITCH_HIGH);

  const beatAt = (x: number) => {
    const beat = Math.round(x / beatPx / snap) * snap;
    return clamp(beat, 0, Math.max(0, lengthBeats - snap));
  };

  const addNoteAt = (x: number, y: number, deferred = false): boolean => {
    const current = notesRef.current;
    const beat = beatAt(x);
    const pitch = pitchAt(y);
    if (current.some((note) => note.pitch === pitch && Math.abs(note.startBeat - beat) < 1e-4))
      return false;
    const note: MidiNote = {
      startBeat: beat,
      lengthBeats: Math.min(snap, Math.max(0.0625, lengthBeats - beat)),
      pitch,
      velocity: 100,
    };
    const next = [...current, note];
    setSelected(next.length - 1);
    if (deferred) defer(next);
    else commit(next);
    return true;
  };

  const removeAt = (index: number) => {
    commit(notesRef.current.filter((_, entry) => entry !== index));
    setSelected(null);
  };

  const removeUnder = (x: number, y: number, deferred = false) => {
    const beat = x / beatPx;
    const pitch = pitchAt(y);
    const index = notesRef.current.findIndex(
      (note) =>
        note.pitch === pitch &&
        beat >= note.startBeat &&
        beat <= note.startBeat + Math.max(note.lengthBeats, snap),
    );
    if (index < 0) return;
    const next = notesRef.current.filter((_, entry) => entry !== index);
    setSelected(null);
    if (deferred) defer(next);
    else commit(next);
  };

  const beginDrag = (event: React.PointerEvent<HTMLElement>, index: number, mode: Drag["mode"]) => {
    event.stopPropagation();
    if (event.button > 0) return;
    rootRef.current?.focus();
    setSelected(index);
    drag.current = {
      mode,
      index,
      startX: num(event.clientX),
      startY: num(event.clientY),
      origin: notes[index],
    };
    gridRef.current?.setPointerCapture?.(event.pointerId);
  };

  const onPointerMove = (event: React.PointerEvent<HTMLDivElement>) => {
    const current = drag.current;
    if (!current) return;

    const dx = num(event.clientX) - current.startX;
    const dy = num(event.clientY) - current.startY;
    const origin = current.origin;

    const next = notesRef.current.map((note, index) => {
      if (index !== current.index) return note;

      if (current.mode === "velocity") {
        return { ...note, velocity: Math.round(clamp(origin.velocity - dy * 2, 1, 127)) };
      }

      if (current.mode === "resize") {
        const length = clamp(
          origin.lengthBeats + Math.round(dx / beatPx / snap) * snap,
          snap,
          lengthBeats,
        );
        return { ...note, lengthBeats: length };
      }

      const beat = clamp(
        origin.startBeat + Math.round(dx / beatPx / snap) * snap,
        0,
        Math.max(0, lengthBeats - origin.lengthBeats),
      );
      const pitch = clamp(origin.pitch + Math.round(-dy / ROW_H), PITCH_LOW, PITCH_HIGH);
      return { ...note, startBeat: beat, pitch };
    });

    defer(next);
  };

  const onGridPointerDown = (event: React.PointerEvent<HTMLDivElement>) => {
    // Right-click is a delete gesture, handled on the note itself.
    if (event.button > 0) return;
    rootRef.current?.focus();
    const { x, y } = pointFrom(event);

    if (tool === "erase") {
      // Erase is a paint gesture too: keep the surface local and commit the
      // whole stroke once, on pointer up.
      removeUnder(x, y, true);
      painting.current = true;
      return;
    }

    // The first paint note is deferred with the rest of the stroke; a plain
    // click still commits because pointer up flushes it.
    const added = tool === "paint" ? addNoteAt(x, y, true) : addNoteAt(x, y);
    if (tool === "paint" && added) painting.current = true;
  };

  const onGridPointerMove = (event: React.PointerEvent<HTMLDivElement>) => {
    if (painting.current) {
      const { x, y } = pointFrom(event);
      if (tool === "erase") removeUnder(x, y, true);
      else addNoteAt(x, y, true);
      return;
    }
    onPointerMove(event);
  };

  const endDrag = (event: React.PointerEvent<HTMLDivElement>) => {
    painting.current = false;
    drag.current = null;
    gridRef.current?.releasePointerCapture?.(event.pointerId);
    flushPending();
  };

  const removeSelected = () => {
    if (selected === null) return;
    removeAt(selected);
  };

  const bars = Math.max(1, Math.round(lengthBeats / 4));

  return (
    <div
      className="pr"
      ref={rootRef}
      tabIndex={0}
      role="application"
      aria-keyshortcuts="Delete Backspace"
      onKeyDown={(event) => {
        if (event.key === "Delete" || event.key === "Backspace") {
          event.preventDefault();
          removeSelected();
        } else if (event.key === "Escape") {
          setSelected(null);
        }
      }}
      aria-label={`Piano roll for ${clipName}`}
    >
      <div className="pr-head">
        <span className="pr-title">{clipName}</span>
        <span className="pr-meta">
          {bars} {bars === 1 ? "BAR" : "BARS"} · {notes.length} NOTES
        </span>

        <div className="pr-tools" role="group" aria-label="Piano roll tools">
          {TOOLS.map((entry) => (
            <button
              key={entry.id}
              className="ws-btn pr-tool"
              aria-pressed={tool === entry.id}
              aria-label={entry.label}
              title={`${entry.label} — ${entry.hint}`}
              onClick={() => setTool(entry.id)}
            >
              {entry.short}
            </button>
          ))}
        </div>

        <label className="pr-snap">
          <span>SNAP</span>
          <select
            aria-label="Note snap"
            value={snap}
            onChange={(event) => setSnap(Number(event.target.value))}
          >
            {SNAPS.map((entry) => (
              <option key={entry.label} value={entry.beats}>
                {entry.label}
              </option>
            ))}
          </select>
        </label>

        <span className="ws-spacer" />
        <button
          className="ws-btn is-icon"
          aria-label="Zoom out"
          title="Zoom out (Ctrl + wheel over the grid)"
          onClick={() => setBeatPx((px) => Math.max(14, Math.round(px * 0.8)))}
        >
          −
        </button>
        <span className="ws-lcd-mini">{beatPx} PX/BEAT</span>
        <button
          className="ws-btn is-icon"
          aria-label="Zoom in"
          title="Zoom in (Ctrl + wheel over the grid)"
          onClick={() => setBeatPx((px) => Math.min(180, Math.round(px * 1.25)))}
        >
          +
        </button>
        <button
          className="ws-btn"
          onClick={removeSelected}
          disabled={selected === null}
          title="Delete the selected note"
        >
          Delete
        </button>
      </div>

      <div className="pr-body">
        <div className="pr-scroll" style={{ width: 44 + gridW }}>
          <div className="pr-keys" style={{ height: gridH }} aria-hidden="true">
            {Array.from({ length: PITCH_HIGH - PITCH_LOW + 1 }, (_, row) => {
              const pitch = PITCH_HIGH - row;
              const black = isBlack(pitch);
              return (
                <span
                  key={pitch}
                  className={black ? "pr-key is-black" : "pr-key"}
                  style={{ top: row * ROW_H, height: ROW_H }}
                >
                  {pitch % 12 === 0 ? noteName(pitch) : ""}
                </span>
              );
            })}
          </div>

          <div
            className={`pr-grid tool-${tool}`}
            ref={gridRef}
            style={{ width: gridW, height: gridH }}
            onPointerDown={onGridPointerDown}
            onPointerMove={onGridPointerMove}
            onPointerUp={endDrag}
            onPointerCancel={endDrag}
            onPointerLeave={() => {
              painting.current = false;
              flushPending();
            }}
            onWheel={(event) => {
              // Plain wheel keeps native scrolling; Ctrl/Alt + wheel zooms.
              if (!(event.ctrlKey || event.metaKey || event.altKey)) return;
              event.preventDefault();
              setBeatPx((px) => clamp(Math.round(px * (event.deltaY < 0 ? 1.2 : 0.84)), 14, 180));
            }}
            title="Click to add a note"
          >
            {Array.from({ length: PITCH_HIGH - PITCH_LOW + 1 }, (_, row) => {
              const pitch = PITCH_HIGH - row;
              return isBlack(pitch) ? (
                <span
                  key={pitch}
                  className="pr-row is-black"
                  style={{ top: row * ROW_H, height: ROW_H }}
                />
              ) : null;
            })}

            {/* Floor + 1: ceil overshot the grid by up to a beat at fractional lengths. */}
            {Array.from({ length: Math.floor(lengthBeats) + 1 }, (_, beat) => (
              <span
                key={beat}
                className={beat % 4 === 0 ? "pr-beat is-bar" : "pr-beat"}
                style={{ left: beat * beatPx }}
              />
            ))}

            {notes.length === 0 && (
              <span className="pr-empty" aria-hidden="true">
                {tool === "erase" ? "NO NOTES · CLICK TO PAINT AWAY" : "NO NOTES · CLICK TO ADD"}
              </span>
            )}

            {notes.map((note, index) => {
              const width = Math.max(6, note.lengthBeats * beatPx - 1);
              return (
                <div
                  key={`${note.pitch}-${note.startBeat}-${index}`}
                  className={selected === index ? "pr-note is-selected" : "pr-note"}
                  role="button"
                  tabIndex={0}
                  aria-pressed={selected === index}
                  style={{
                    left: note.startBeat * beatPx,
                    top: (PITCH_HIGH - note.pitch) * ROW_H,
                    width,
                    height: ROW_H - 1,
                  }}
                  title={`${noteName(note.pitch)} · beat ${note.startBeat.toFixed(2)} · vel ${note.velocity}`}
                  aria-label={`Note ${noteName(note.pitch)} at beat ${note.startBeat}`}
                  onKeyDown={(event) => {
                    if (event.key === "Delete" || event.key === "Backspace") {
                      event.preventDefault();
                      event.stopPropagation();
                      removeAt(index);
                    } else if (event.key === "Enter" || event.key === " ") {
                      event.preventDefault();
                      setSelected(index);
                    }
                  }}
                  onContextMenu={(event) => {
                    event.preventDefault();
                    event.stopPropagation();
                    removeAt(index);
                  }}
                  onPointerDown={(event) => {
                    if (event.button === 2) {
                      event.stopPropagation();
                      removeAt(index);
                      return;
                    }
                    if (event.altKey || tool === "erase") {
                      event.stopPropagation();
                      removeAt(index);
                      return;
                    }
                    const box = (event.currentTarget as HTMLElement).getBoundingClientRect();
                    const nearEdge = num(event.clientX) - box.left > box.width - 7;
                    beginDrag(event, index, nearEdge ? "resize" : "move");
                  }}
                >
                  <span className="pr-note-grip" />
                </div>
              );
            })}
          </div>
        </div>

        <div className="pr-velocity" aria-label="Note velocity">
          <span className="pr-velocity-label">VEL</span>
          <div className="pr-velocity-lane">
            {notes.map((note, index) => (
              <span
                key={`v-${index}`}
                className={selected === index ? "pr-velocity-bar is-selected" : "pr-velocity-bar"}
                role="slider"
                tabIndex={0}
                aria-label={`Velocity for ${noteName(note.pitch)} at beat ${note.startBeat.toFixed(2)}`}
                aria-valuemin={1}
                aria-valuemax={127}
                aria-valuenow={note.velocity}
                style={{
                  /* Pixel left (same beatPx scale as the notes): percent
                     drifted from the notes when gridW hit its 120px floor. */
                  left: note.startBeat * beatPx,
                  width: 6,
                  height: `${Math.max(8, (note.velocity / 127) * 100)}%`,
                }}
                title={`Velocity ${note.velocity} — ↑/↓ to adjust`}
                onPointerDown={(event) => beginDrag(event, index, "velocity")}
                onKeyDown={(event) => {
                  const step = event.shiftKey ? 1 : 5;
                  let next = note.velocity;
                  if (event.key === "ArrowUp" || event.key === "ArrowRight")
                    next = Math.min(127, note.velocity + step);
                  else if (event.key === "ArrowDown" || event.key === "ArrowLeft")
                    next = Math.max(1, note.velocity - step);
                  else if (event.key === "Home") next = 100;
                  else if (event.key === "End") next = 127;
                  else return;
                  event.preventDefault();
                  event.stopPropagation();
                  const nextNotes = notes.map((entry, entryIndex) =>
                    entryIndex === index ? { ...entry, velocity: next } : entry,
                  );
                  commit(nextNotes);
                  setSelected(index);
                }}
              />
            ))}
          </div>
        </div>
      </div>

      <div className="pr-hint">
        DRAG NOTES TO MOVE · DRAG THE RIGHT EDGE TO RESIZE
        {persisted ? " · SAVED WITH THE SESSION" : " · CLIP DRAFT"}
      </div>
    </div>
  );
}
