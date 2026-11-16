# Mixer Density (16 Tracks at 1280) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development
> (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use
> checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make a 16-track session fit a 1280 px window without horizontal scrolling, by adding a
compact density step and a "Fit strips" control — no functionality removed, nothing hidden.

**Architecture:** Strip width is currently fixed in CSS (`ui/src/workspace/refine.css:1279`, `.ws-strip`),
so it cannot respond to track count. This plan lifts the decision into one pure function so it is
testable without a browser, then drives a `data-density` attribute on the mixer grid from it.

**Tech Stack:** React 19 + TypeScript, Vitest, plain CSS custom properties.

**Spec:** `docs/superpowers/plans/2026-11-16-readiness-to-95/README.md` and `.planning/ROADMAP.md`
Phase 4.

## Global Constraints

- No control may disappear in compact density; only spacing, meter width and label sizes change.
- The master strip keeps its own width and never scrolls out of view.
- No fixed pixel value may be duplicated between CSS and TypeScript: the TS constants drive the CSS
  custom properties (`--strip-min`, `--master-min`).
- Vitest cannot measure layout, so every assertion is on the pure function or on the rendered
  `data-density` attribute — never on `getBoundingClientRect`.

## Review Focus

- **17 tracks at 1280** — must stay compact and scroll by exactly one strip, not overflow the master.
- **Resizing the window while compact** — crossing the threshold must not lose the selected track or
  reset a fader drag.
- **User picks "Normal" manually with 16 tracks** — the manual choice wins over the automatic
  threshold until the window changes again.
- **A very narrow window (800 px)** — must scroll rather than collapse strips below a usable width.
- **Track names longer than the strip** — must ellipsize, never wrap and grow the strip height.

---

## Task 1: A pure density decision

**Files:**
- Create: `ui/src/workspace/mixerLayout.ts`
- Test: `ui/src/workspace/mixerLayout.test.ts`

**Interfaces:**
- Produces:

```ts
export type Density = "normal" | "compact";

/** 112 px per strip normal, 72 px compact; master is 96 px in both. */
export function densityFor(trackCount: number, viewportWidth: number, minWidth = 260): Density;

/** Strip min-width in px for a density; mirrors the CSS custom property. */
export function stripMinWidthPx(density: Density): number;
```

- [ ] **Step 1: Write the failing test**

```ts
import { densityFor, stripMinWidthPx } from "./mixerLayout";

it("fits 16 tracks at 1280 by going compact", () => {
  expect(densityFor(16, 1280)).toBe("compact");
  expect(16 * stripMinWidthPx("compact") + 96).toBeLessThanOrEqual(1280);
});

it("stays normal for 8 tracks at 1280", () => {
  expect(densityFor(8, 1280)).toBe("normal");
});

it("keeps compact at 17 tracks and scrolls instead of collapsing", () => {
  expect(densityFor(17, 1280)).toBe("compact");
  expect(stripMinWidthPx("compact")).toBeGreaterThanOrEqual(72);
});

it("does not collapse below the usable minimum on an 800 px window", () => {
  expect(stripMinWidthPx(densityFor(16, 800))).toBe(72);
});
```

- [ ] **Step 2: Run it and watch it fail**

Run: `cd ui && npx vitest run src/workspace/mixerLayout.test.ts`
Expected: FAIL — module not found.

- [ ] **Step 3: Implement the function**

`densityFor` returns `"normal"` when `trackCount * 112 + 96 <= viewportWidth - minWidth`, else
`"compact"`. `stripMinWidthPx` returns `112` or `72`.

- [ ] **Step 4: Run it and watch it pass.**

- [ ] **Step 5: Commit** — `feat(mixer): add a pure strip-density decision`.

---

## Task 2: Apply density to the grid

**Files:**
- Modify: `ui/src/workspace/Mixer.tsx` (grid container)
- Modify: `ui/src/workspace/refine.css` (`.ws-strip` min-width, new `[data-density="compact"]` rules)
- Test: `ui/src/workspace/Mixer.test.tsx`

**Interfaces:**
- Consumes: `densityFor`, `stripMinWidthPx` from Task 1.
- Produces: the grid element carries `data-density={"normal" | "compact"}` and a
  `style={{ ["--strip-min"]: `${stripMinWidthPx(d)}px` }}` custom property.

- [ ] **Step 1: Write the failing test**

```tsx
it("renders 16 tracks in compact density at 1280", () => {
  render(<Mixer backend={backend} state={stateWithTracks(16)} viewportWidth={1280} />);
  expect(screen.getByRole("group", { name: "Mixer" })).toHaveAttribute("data-density", "compact");
});
```

- [ ] **Step 2: Run it and watch it fail** — no `data-density` attribute.
- [ ] **Step 3: Wire it**

Replace the hard-coded `min-width: 112px` on `.ws-strip` with `min-width: var(--strip-min, 112px)`,
add `.ws-mixer[data-density="compact"]` rules for gap, meter width and label size, and set the
attribute plus the custom property from `densityFor(trackCount, viewportWidth)`.

- [ ] **Step 4: Gate** — vitest PASS (the existing Mixer tests stay green).
- [ ] **Step 5: Commit** — `feat(mixer): drive strip width from a density attribute`.

---

## Task 3: "Fit strips" control with a manual override

**Files:**
- Modify: `ui/src/workspace/HeaderBar.tsx` or the mixer toolbar (whichever already hosts view toggles)
- Test: `ui/src/workspace/Mixer.test.tsx`

**Interfaces:**
- Produces: a toggle button labelled **Fit strips**, `aria-pressed`, whose manual choice
  (`"auto" | "normal" | "compact"`) survives until `viewportWidth` changes again.

- [ ] **Step 1: Write the failing test**

```tsx
it("keeps the user's manual density until the window changes", async () => {
  const { rerender } = render(<Mixer backend={backend} state={stateWithTracks(16)} viewportWidth={1280} />);
  await userEvent.click(screen.getByRole("button", { name: "Fit strips" }));
  expect(grid()).toHaveAttribute("data-density", "normal");
  rerender(<Mixer backend={backend} state={stateWithTracks(16)} viewportWidth={1300} />);
  expect(grid()).toHaveAttribute("data-density", "compact");
});
```

- [ ] **Step 2: Run it and watch it fail.**
- [ ] **Step 3: Implement**: hold `densityChoice` in component state, reset it to `"auto"` in an
  effect keyed on `viewportWidth`.
- [ ] **Step 4: Gate** — vitest PASS; `cd ui && npx tsc -b` clean; `vite build` succeeds.
- [ ] **Step 5: Commit** — `feat(mixer): fit-strips toggle with a manual override`.

---

## Self-Review

- Roadmap Phase 4 asks for either a density step or a fit toggle; this plan ships both, because the
  toggle is what makes the automatic decision discoverable.
- Review Focus: 17 tracks is Task 1's third test plus `>= 72px`; resize behaviour is Task 3's reset
  effect; manual override is Task 3's first assertion; 800 px is Task 1's fourth test; long names are
  handled in Task 2's CSS (`text-overflow: ellipsis`, `white-space: nowrap`) and must not change the
  strip height.
- The TS constants and the CSS defaults are the same numbers (112 normal, 72 compact, 96 master),
  with the TS side authoritative through the custom property.
