/**
 * How many pixels a mixer strip needs, and whether the strips fit.
 *
 * This is the single source of truth for strip geometry: the mixer writes
 * `--strip-min` from here, and the CSS falls back to the same numbers. Keeping
 * the decision pure means the 16-track case is testable without a browser.
 */

export type Density = "normal" | "compact";

const NORMAL_STRIP_PX = 112;
const COMPACT_STRIP_PX = 72;

/** The master strip keeps its width in both densities. */
export const MASTER_STRIP_PX = 96;

/** Room the mixer keeps for chrome so strips never touch the window edge. */
const MIN_USABLE_WIDTH_PX = 260;

export function stripMinWidthPx(density: Density): number {
  return density === "compact" ? COMPACT_STRIP_PX : NORMAL_STRIP_PX;
}

/**
 * The density that lets `trackCount` strips plus the master fit `viewportWidth`
 * without scrolling. Never returns anything narrower than the compact step: a
 * very narrow window scrolls rather than collapsing the strips.
 */
export function densityFor(
  trackCount: number,
  viewportWidth: number,
  minWidth = MIN_USABLE_WIDTH_PX,
): Density {
  const needed = trackCount * NORMAL_STRIP_PX + MASTER_STRIP_PX;
  return needed <= viewportWidth - minWidth ? "normal" : "compact";
}
