# Readiness to 95 — plan set (2026-11-16)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development
> (recommended) or superpowers:executing-plans to implement each plan task-by-task. Steps use
> checkbox (`- [ ]`) syntax for tracking.

**Goal:** Lift every department of NCSound DAW from its current score to at least 95/100 on the
function-and-usability scale below, without regressing the pinned render digest or the 195-test
native suite.

**Architecture:** Five independent plans. Each one ships working, testable software on its own and
is ordered so later plans build on earlier interfaces rather than re-deciding them. No plan touches
the `AudioEngine` public API except plan 01, which adds one gate.

**Tech Stack:** C++20 / JUCE 8.0.17 native in `app/Source`; React + TypeScript + Vitest in `ui/`;
bridge routes registered in `app/Source/WebViewBridge.cpp` and generated into
`ui/src/lib/nativeRoutes.generated.ts`; CMake + Ninja + MSVC 14.44; CPack for packaging.

**Spec:** this README (the score table, the ordering and the gates) plus `.planning/ROADMAP.md`,
which remains the authority on what each phase means. This plan set supersedes the "OPEN" markers in
that roadmap only where a task below closes them.

---

## Where the score stands

Re-scored 2026-11-16 from the tree at `acad814` (195 native tests, 118 cases + 1 new automation
case, 110 UI tests, 58 routes). The baseline column is the original scoring at `0c6ae39`.
Weights are the share of a shippable DAW each department carries.

| Department | Weight | Baseline | Now | Target | Plan |
| --- | --- | --- | --- | --- | --- |
| Core engine / DSP | 15 | 95 | 95 | 95 | — (done) |
| Recording | 10 | 85 | 85 | 95 | hardware gate only, no code plan |
| Arrangement / editing | 15 | 85 | 88 | 95 | 01 (AUTO landed); rest needs plan 03 |
| Mixing / routing | 12 | 60 | 92 | 95 | **01 — done** |
| Plugin hosting / catalogue | 12 | 65 | 65 | 95 | **02** |
| Session / export | 10 | 70 | 70 | 95 | **03** |
| Usability / layout | 12 | 80 | 90 | 95 | **04 — done** |
| Performance / stability | 8 | 75 | 75 | 95 | **05** Tasks 1-2 |
| Distribution | 6 | 35 | 35 | 95 | **05** Tasks 3-5 |

Weighted overall: **75.2 → 80.7**. Projected once the remaining plans land: **≈95.4**.

Why the two finished departments stop short of 95 rather than jumping straight there:

- **Mixing / routing 92, not 95.** Bypass, drag-reorder, the hosted editor and master A/B are all
  reachable and tested, but nothing has been exercised by hand in the running app, and an
  occupied-slot drop is refused rather than swapped, so the chain cannot be reordered by swapping.
  The last three points are interactive verification plus that swap path, if it is wanted.
- **Usability 90, not 95.** Strip density and the fit toggle are unit-tested and typechecked, but the
  16-track layout has not been looked at on a real window at any width.
- **Arrangement 88, not 95.** Plan 01 only carried the AUTO read toggle; lane editing, comping and
  clip-level polish are outside it. Plan 03's save/export proof is what moves this department next.

## The plans

| File | Department | Tasks | Status | Depends on |
| --- | --- | --- | --- | --- |
| `01-insert-routing.md` | Mixing / routing, insert UX | 5 | **done** (`721f35c`, `c7f2299`, `acad814`) | — |
| `02-plugin-catalogue.md` | Catalogue reliability | 5 | next | — |
| `03-session-export.md` | Session + export proof | 5 | queued | — |
| `04-mixer-density.md` | 16-track mixer fit | 3 | **done** (`016c75a`, `b7761fb`, `1841a33`) | — |
| `05-packaging-performance.md` | Soak, packaging, legal | 5 | queued (2 need hardware / `makensis`) | 01-04 for the soak build |

Run order: **04 → 01 → 02 → 03 → 05**. 04 and 01 are landed. 02 comes next because it is the same
kind of work as 01 — catalogue engine changes plus a few routes and browser UI — and doing it now
keeps one `gen:routes` cycle and one app relink in flight; 03 needs the app to be trustworthy first;
05 documents the result and soaks the finished build.

## Next steps

1. **Plan 02, task 1 — per-bundle scan progress.** `ScannerSuperprocess` already kills timed-out
   workers and `PluginCatalog` already exposes `scanProgress` / `isScanning` / `cancelScan`; the gap
   is that progress only advances between bundles. Make it advance per bundle and pin it with a test.
2. **Plan 02, task 2 — persistent index keyed by size and mtime.** Cache the scan so a cold start
   does not re-walk every VST3 folder.
3. **Plan 02, task 3 — cold start uses the index**, falling back to a full scan only when the index
   is absent or stale.
4. **Plan 02, task 4 — no permanently-`Missing` rows.** A rescan must clear a row whose plug-in
   reappeared.
5. **Plan 02, task 5 — show scan progress in the browser** so the user sees what the scanner is
   doing and can cancel. Closes catalogue at ≈95.
6. **Plan 03, tasks 1-5 — session and export proof.** Project file round-trip, a failed save leaving
   the original intact, mixdown to a real WAV, one stem per track, and the UI reporting each
   outcome. `renderToWav` / `exportStems` are fully implemented and untested — the work is proof,
   not implementation. Closes session/export at ≈95.
7. **Plan 05, tasks 1-2 — soak harness and a recorded 30-minute run.** Needs a quiet machine; the
   suite already timed out once under load, so run it when the box is not also building.
8. **Plan 05, tasks 3-5 — packaging, notices, human gates.** Task 3 needs `makensis` and task 5
   records the gates that only a human with hardware can sign off (recording 85 → 95).

Interactive verification is outstanding for both finished plans: nothing has been clicked through in
the running app — bypass, drag-reorder, the editor window, master A/B recall, AUTO, and the 16-track
strip layout are covered by unit tests and compiled route tables only.

## Definition of done for the set

Every plan ends with the same gate, and no task in it may be checked off without it:

```
cmd.exe //c call build/_nc_mixrack_build.cmd          -> nc_mixrack_build.exit = 0
cmd.exe //c call build/_ncbuild_app.cmd               -> EXIT=0
ctest --test-dir build -C Release                     -> 195/195, EXIT=0
cd ui && npx tsc -b && npx vitest run                 -> 0 errors, all tests pass
render digest                                          -> 0x1b5398dd4a9f297a
```

The digest is the DSP gate: any plan task that moves it is wrong, not "a new baseline".

## Global constraints (all plans)

- Native builds run **only** through `build/_nc_mixrack_build.cmd` and `build/_ncbuild_app.cmd`
  from the repo root; a bare `cmake --build` can miss the MSVC environment.
- After adding, renaming or removing a native route, run `cd ui && npm run gen:routes`.
  `ui/src/lib/nativeRoutes.generated.ts` is generated from `app/Source/WebViewBridge.cpp` and is
  never edited by hand.
- Route names are camelCase and match the JS method exactly (`loadPlugin` ↔ `loadPlugin(track, slot, id)`).
- Command output is capped at ~295 s; `ctest` takes 450-1600 s on this machine depending on load —
  run it in the background to a log with an `EXIT=` marker and poll.
- Record what was verified and what was not in `DAW_WORK_LOG.md` at the end of each plan.
- Never stage `verify-wheel-app.png`.
