import { useEffect, useState } from "react";

const DEFAULT_HINTS = "V SELECT / T TRIM / F FADE / G GAIN / X SPLIT / E ERASE";

export interface StatusBarProps {
  message?: string;
  alert?: boolean;
  activeTracks: number;
  trackCapacity: number;
  clipCount: number;
  pluginCount: number;
  /** View-specific shortcut line; the counts live only on the right. */
  hints?: string;
}

/** Native status strip: one LED, the last action, the shortcuts for the
    surface in view, and the session counters with a clock. */
export function StatusBar({
  message,
  alert = false,
  activeTracks,
  trackCapacity,
  clipCount,
  pluginCount,
  hints = DEFAULT_HINTS,
}: StatusBarProps) {
  const [clock, setClock] = useState(() => new Date());

  useEffect(() => {
    const timer = setInterval(() => setClock(new Date()), 60_000);
    return () => clearInterval(timer);
  }, []);

  const time = `${String(clock.getHours()).padStart(2, "0")}:${String(clock.getMinutes()).padStart(2, "0")}`;
  const overCapacity = activeTracks > trackCapacity;
  // Counts belong in one place only; the left side reports the last action.
  const text = message || "READY";

  return (
    // role="status" only on the live message: the clock must not re-announce
    // the whole strip every minute, and the hints stay readable to AT.
    <footer className="ws-status">
      <span className={alert ? "ws-status-led is-alert" : "ws-status-led"} aria-hidden="true" />
      <span className="ws-status-msg" role="status" title={text}>
        {text}
      </span>
      <span className="ws-status-hints" title={hints}>
        <span className="ws-sr-only">Shortcuts: </span>
        {hints}
      </span>
      <span className="ws-status-right" aria-hidden="true">
        {overCapacity ? `${activeTracks}/${trackCapacity} TRACKS` : `${activeTracks} OF ${trackCapacity} TRACKS`} / {clipCount} CLIPS / {pluginCount} PLUG-INS / {time}
      </span>
    </footer>
  );
}
