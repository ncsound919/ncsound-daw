/** Eight insert slots per track, shown as two banks so a strip stays short
 *  enough to fit on screen. These are the only indices the load routes accept. */
export const INSERT_SLOTS = [0, 1, 2, 3, 4, 5, 6, 7] as const;

export const INSERT_BANKS = [
  { id: "a", label: "1–4", slots: [0, 1, 2, 3] as const },
  { id: "b", label: "5–8", slots: [4, 5, 6, 7] as const },
] as const;

export type WorkspaceView =
  "arrange" | "mix" | "inspector" | "piano" | "browser" | "automation" | "pitch" | "settings";

/** Every surface the native app owns, in the order its menus reach them.
 *  Browser lives only as the insert-load dialog and pitch lives in the
 *  transport / channel header, so neither is a top-level tab. */
export const WORKSPACE_VIEWS: { id: WorkspaceView; label: string; title: string }[] = [
  { id: "arrange", label: "Arrange", title: "Arrangement timeline and track headers" },
  { id: "mix", label: "Mix", title: "Channel strips, sends and master" },
  { id: "inspector", label: "Channel", title: "Selected track: inserts, faceplate, sends, output" },
  { id: "piano", label: "Piano Roll", title: "Note editor for the selected clip" },
  { id: "automation", label: "Automation", title: "Volume and pan automation lanes" },
  { id: "settings", label: "Settings", title: "Appearance and behaviour customisation" },
];

/** 1-based fractional bar to the host's BARS|BEATS|TICK readout (4/4). */
export function formatBarsBeats(positionBar: number): { main: string; small: string } {
  const safe = Number.isFinite(positionBar) ? Math.max(1, positionBar) : 1;
  const bar = Math.floor(safe);
  const beatsInto = (safe - bar) * 4;
  const beat = Math.floor(beatsInto) + 1;
  const tick = Math.floor((beatsInto - Math.floor(beatsInto)) * 960);
  const pad = (n: number, width: number) => String(n).padStart(width, "0");
  return { main: `${pad(bar, 3)}|${pad(beat, 2)}|${pad(tick, 3)}`, small: "BARS / BEATS" };
}

const ZOOM_STEP = 0.9;

/**
 * Pointer-anchored timeline zoom. `anchorRatio` is 0..1 across the lane, so
 * the bar under the pointer stays under it. Zoom out is the exact reciprocal
 * of zoom in: an in/out pair returns to the same window instead of creeping
 * down the timeline the way a hardcoded 1.11 did.
 */
export function zoomWindow(
  viewStart: number,
  visibleBars: number,
  anchorRatio: number,
  direction: "in" | "out",
  minBars: number,
  maxBars: number,
): { viewStart: number; visibleBars: number } {
  const ratio = Math.max(0, Math.min(1, anchorRatio));
  const factor = direction === "in" ? ZOOM_STEP : 1 / ZOOM_STEP;
  const next = Math.max(minBars, Math.min(maxBars, visibleBars * factor));
  const anchorBar = viewStart + ratio * visibleBars;
  const nextStart = Math.max(1, Math.min(Math.max(1, maxBars - next), anchorBar - ratio * next));
  return { viewStart: nextStart, visibleBars: next };
}

/** Left position, in percent, of a bar inside the visible window. */
export function barPct(bar: number, viewStart: number, visibleBars: number): number {
  if (!(visibleBars > 0)) return 0;
  return ((bar - viewStart) / visibleBars) * 100;
}

/**
 * Fader readout. The fader floor is -60 dB, so a parked fader reads "-60.0"
 * rather than a bare "-inf"; true digital silence (peak 0) reads "—".
 * Both carry their unit from the caller, never a baked "dB" here.
 */
export function dbLabel(db: number): string {
  if (!Number.isFinite(db)) return "—";
  if (db <= -59.9) return "-60.0";
  return `${db > 0 ? "+" : ""}${db.toFixed(1)}`;
}

/** Peak 0..1 to a dB height on the meter law: -60 dB = 0%, -12 dB = 75%,
 *  0 dB (peak 1.0) = 100%. Hotter peaks clamp at 100. */
export function meterPct(peak: number): number {
  const clamped = Math.max(0, Math.min(1, peak));
  if (clamped <= 0) return 0;
  const db = 20 * Math.log10(clamped);
  const v = Math.max(-60, Math.min(0, db));
  const norm = v <= -12 ? ((v + 60) / 48) * 0.75 : 0.75 + ((v + 12) / 12) * 0.25;
  return Math.max(2, norm * 100);
}

/** Fader curve: -60.. -12 spans 0..0.75, -12..+6 spans 0.75..1. */
export function faderNorm(db: number): number {
  const v = Math.max(-60, Math.min(6, db));
  if (v <= -12) return ((v + 60) / 48) * 0.75;
  return 0.75 + ((v + 12) / 18) * 0.25;
}

/**
 * Closed polygon for a mirrored waveform in a 0..100 box: the top edge runs
 * left to right, the mirrored bottom edge runs back, so one fill draws both
 * halves. Returns an empty string when there is nothing to draw.
 */
export function waveformPoints(peaks: readonly number[]): string {
  if (peaks.length < 2) return "";

  const top: string[] = [];
  const bottom: string[] = [];
  const last = peaks.length - 1;

  peaks.forEach((peak, index) => {
    const x = ((index / last) * 100).toFixed(2);
    const amp = Math.max(0, Math.min(1, peak)) * 46;
    top.push(`${x},${(50 - amp).toFixed(2)}`);
    bottom.push(`${x},${(50 + amp).toFixed(2)}`);
  });

  return `${top.join(" ")} ${bottom.reverse().join(" ")}`;
}

/**
 * Scale that fits a fixed-size faceplate preview inside the editor box. The
 * panel is drawn at its design size (1320x780); without fitting, the preview is
 * cropped to its top-left corner. Clamped so text stays legible and a roomy
 * window never stretches the face past life size.
 */
export function fitScale(
  availableWidth: number,
  availableHeight: number,
  panelWidth: number,
  panelHeight: number,
  min = 0.3,
  max = 1,
): number {
  if (!(panelWidth > 0) || !(panelHeight > 0)) return 0.65;
  if (!(availableWidth > 0) || !(availableHeight > 0)) return min;
  const fit = Math.min(availableWidth / panelWidth, availableHeight / panelHeight);
  if (!Number.isFinite(fit)) return min;
  return Math.max(min, Math.min(max, fit));
}

/** Track identity tints, mirroring theme::trackTints in app/Source/Theme.h. */
export const TRACK_TINTS = [
  "#ff8a3c",
  "#ff5f3a",
  "#ffc247",
  "#8fe06a",
  "#e8a04c",
  "#d98b6a",
  "#4fd6c0",
  "#e07ab8",
] as const;

export function tintForTrack(track: number): string {
  return TRACK_TINTS[((track % TRACK_TINTS.length) + TRACK_TINTS.length) % TRACK_TINTS.length];
}
