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

Scored 2026-11-16 from the tree at `0c6ae39` (195 native tests, 118 cases, 81 UI tests, 52 routes).
Weights are the share of a shippable DAW each department carries.

| Department | Weight | Now | Target | Plan |
| --- | --- | --- | --- | --- |
| Core engine / DSP | 15 | 95 | 95 | — (done) |
| Recording | 10 | 85 | 95 | hardware gate only, no code plan |
| Arrangement / editing | 15 | 85 | 95 | 01, Task 5 (AUTO read) |
| Mixing / routing | 12 | 60 | 95 | **01** |
| Plugin hosting / catalogue | 12 | 65 | 95 | **02** |
| Session / export | 10 | 70 | 95 | **03** |
| Usability / layout | 12 | 80 | 95 | **04** |
| Performance / stability | 8 | 75 | 95 | **05** Tasks 1-2 |
| Distribution | 6 | 35 | 95 | **05** Tasks 3-5 |

Projected overall once all five land: **≈95.4**.

## The plans

| File | Department | Tasks | Depends on |
| --- | --- | --- | --- |
| `01-insert-routing.md` | Mixing / routing, insert UX | 5 | — |
| `02-plugin-catalogue.md` | Catalogue reliability | 5 | — |
| `03-session-export.md` | Session + export proof | 5 | — |
| `04-mixer-density.md` | 16-track mixer fit | 3 | — |
| `05-packaging-performance.md` | Soak, packaging, legal | 5 | 01-04 for the soak build |

Run order: **04 → 01 → 02 → 03 → 05**. 04 is self-contained layout work and gets a quick win on
the board; 01 and 02 both add routes, so doing them back to back means one `gen:routes` cycle and
one app relink rather than two; 03 needs the app to be trustworthy first; 05 documents the result and
soaks the finished build.

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
