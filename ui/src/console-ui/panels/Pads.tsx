// GENERATED from console-ui — edit there, then run `npm run sync`.
import { useRef, useState, type DragEvent, type KeyboardEvent, type PointerEvent } from "react";

export interface PadInfo {
  index: number;
  /** Sample (or slice) name; empty when the pad has no sample. */
  name: string;
  hasSample: boolean;
  /** Slice number when the pad holds a chop, for the corner tag. */
  slice?: number;
}

export const BANKS = ["A", "B", "C", "D", "E", "F", "G", "H"] as const;
export const padLabel = (index: number) => `${BANKS[Math.floor(index / 16)]}${String((index % 16) + 1).padStart(2, "0")}`;

/**
 * MPC-style 4×4 pad grid (pad 1 bottom-left, like the hardware).
 * Velocity comes from where the pad is struck: the centre hits hardest.
 * Full Level forces 127; 16 Levels turns the grid into 16 velocities of the
 * selected pad. Both are UI-side and need no engine support.
 */
export function Pads({ pads, bank, selected, onSelect, onTrigger, onRelease, onDropFiles, fullLevel = false, sixteenLevels = false, size = 86 }: {
  pads: PadInfo[]; bank: number; selected: number;
  onSelect: (index: number) => void; onTrigger: (index: number, velocity: number) => void;
  /** Pad let go (pointer up / key up): ends Gate-mode voices and note repeat. */
  onRelease?: (index: number) => void;
  /** Files dropped on a pad (the first lands on that pad, the rest on the following pads). Omit when the host cannot load drops. */
  onDropFiles?: (index: number, files: File[]) => void;
  fullLevel?: boolean; sixteenLevels?: boolean; size?: number;
}) {
  const [hits, setHits] = useState<Record<number, number>>({});
  const [dropSlot, setDropSlot] = useState<number | null>(null);
  const hasFiles = (e: DragEvent) => Array.from(e.dataTransfer?.types ?? []).includes("Files");
  const dropProps = (slot: number) => onDropFiles ? {
    onDragOver: (e: DragEvent<HTMLButtonElement>) => { if (!hasFiles(e)) return; e.preventDefault(); e.dataTransfer.dropEffect = "copy"; setDropSlot(slot); },
    onDragLeave: () => setDropSlot((s) => (s === slot ? null : s)),
    onDrop: (e: DragEvent<HTMLButtonElement>) => {
      e.preventDefault(); setDropSlot(null);
      const files = Array.from(e.dataTransfer.files);
      if (files.length) { onSelect(bank * 16 + slot); onDropFiles(bank * 16 + slot, files); }
    },
  } : {};
  const timers = useRef<Record<number, number>>({});

  const flash = (slot: number, vel: number) => {
    setHits((h) => ({ ...h, [slot]: vel }));
    window.clearTimeout(timers.current[slot]);
    timers.current[slot] = window.setTimeout(() => setHits((h) => { const n = { ...h }; delete n[slot]; return n; }), 140);
  };

  const strike = (slot: number, vel: number) => {
    const index = bank * 16 + slot;
    if (sixteenLevels) { onTrigger(selected, (slot + 1) / 16); flash(slot, (slot + 1) / 16); return; }
    const v = fullLevel ? 1 : vel;
    onSelect(index);
    onTrigger(index, v);
    flash(slot, v);
  };

  // The pad index each slot is holding, so the release goes to the pad that
  // was struck even if the bank changed meanwhile (16 Levels holds `selected`).
  const held = useRef<Record<number, number>>({});
  const release = (slot: number) => {
    const index = held.current[slot];
    if (index === undefined) return;
    delete held.current[slot];
    onRelease?.(index);
  };

  const onDown = (slot: number) => (e: PointerEvent<HTMLButtonElement>) => {
    if (e.button !== 0) return;
    if (onRelease) {
      e.currentTarget.setPointerCapture?.(e.pointerId);
      held.current[slot] = sixteenLevels ? selected : bank * 16 + slot;
    }
    const r = e.currentTarget.getBoundingClientRect();
    const dx = (e.clientX - r.left) / r.width - 0.5, dy = (e.clientY - r.top) / r.height - 0.5;
    const vel = Math.max(0.2, 1 - Math.hypot(dx, dy) * 1.4);
    strike(slot, Math.round(vel * 127) / 127);
  };
  const onKey = (slot: number) => (e: KeyboardEvent<HTMLButtonElement>) => {
    if (e.key === " " || e.key === "Enter") {
      e.preventDefault();
      if (e.repeat) return;
      if (onRelease) held.current[slot] = sixteenLevels ? selected : bank * 16 + slot;
      strike(slot, e.shiftKey ? 1 : 0.8);
    }
  };
  const onKeyUp = (slot: number) => (e: KeyboardEvent<HTMLButtonElement>) => {
    if (e.key === " " || e.key === "Enter") release(slot);
  };

  // Hardware order: rows top→bottom are pads 13-16, 9-12, 5-8, 1-4.
  const order = [12, 13, 14, 15, 8, 9, 10, 11, 4, 5, 6, 7, 0, 1, 2, 3];
  return (
    <div role="group" aria-label={`Pads, bank ${BANKS[bank]}`} style={{ display: "grid", gridTemplateColumns: `repeat(4, ${size}px)`, gap: 8 }}>
      {order.map((slot) => {
        const index = bank * 16 + slot;
        const p = pads[index];
        const isSel = !sixteenLevels && index === selected;
        const hit = hits[slot];
        const lit = p?.hasSample || sixteenLevels;
        const dropping = dropSlot === slot;
        const glowA = hit !== undefined ? 0.35 + hit * 0.55 : lit ? 0.16 : 0;
        return (
          <button key={slot} type="button" onPointerDown={onDown(slot)} onPointerUp={() => release(slot)} onPointerCancel={() => release(slot)}
            onKeyDown={onKey(slot)} onKeyUp={onKeyUp(slot)} {...dropProps(slot)}
            aria-label={`Pad ${padLabel(index)}${sixteenLevels ? `, velocity ${Math.round(((slot + 1) / 16) * 127)}` : p?.hasSample ? `, ${p.name}` : ", empty"}`}
            aria-pressed={isSel}
            style={{
              position: "relative", width: size, height: size * 0.84, padding: "7px 9px", borderRadius: 8, border: "1px solid #000", cursor: "pointer", textAlign: "left",
              display: "flex", flexDirection: "column", justifyContent: "space-between",
              background: `radial-gradient(circle at 50% 55%, rgba(255,138,0,${glowA}), transparent 72%), radial-gradient(circle at 50% 35%, #414146, #313135 80%)`,
              boxShadow: [
                dropping ? "0 0 0 2px #3cd6dd" : isSel ? "0 0 0 2px #ff8a00" : "0 0 0 1px #000",
                hit !== undefined ? `0 0 ${10 + hit * 20}px rgba(255,138,0,${0.3 + hit * 0.4})` : "",
                "inset 0 1px 0 rgba(255,255,255,.1)", `inset 0 ${hit !== undefined ? 1 : -4}px 6px rgba(0,0,0,.4)`, `0 ${hit !== undefined ? 1 : 3}px 0 #000`,
              ].filter(Boolean).join(", "),
              transform: hit !== undefined ? "translateY(2px)" : "none", transition: "transform .06s, box-shadow .12s",
            }}>
            <span style={{ display: "flex", justifyContent: "space-between", width: "100%", fontFamily: "var(--cs-font-mono)", fontSize: 11, color: lit ? "#ffae3a" : "#6f6c66" }}>
              <span>{padLabel(index)}</span>
              {p?.slice !== undefined && !sixteenLevels && <span style={{ color: "#3cd6dd" }}>S{String(p.slice + 1).padStart(2, "0")}</span>}
              {sixteenLevels && <span>{Math.round(((slot + 1) / 16) * 127)}</span>}
            </span>
            <span style={{ fontStretch: "75%", fontWeight: 700, fontSize: 11, letterSpacing: ".06em", textTransform: "uppercase", color: lit ? "#f1eee8" : "#6f6c66", whiteSpace: "nowrap", overflow: "hidden", textOverflow: "ellipsis", width: "100%" }}>
              {sixteenLevels ? pads[selected]?.name || "Level" : p?.hasSample ? p.name : "—"}
            </span>
          </button>
        );
      })}
    </div>
  );
}
