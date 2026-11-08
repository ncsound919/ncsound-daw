# Remaining Gaps — Research & Decisions

**Date:** 2026-11-07
**Scope:** Undo/redo depth, router keyboard clip editing, crossfades/comping,
export variants (stems/MP3/FLAC/dither/LUFS), autosave, collab production, CI matrix.
**Method:** source audit of `engine/`, `app/Source/` (UndoStack, ClipEdit, ArrangeView,
AudioEngine::renderToWav, MainComponent menus/keys, ProjectFile, collab relay, ci.yml)
+ public best-practice sources below. No implementation assumptions from ROADMAP.md
(stale for engine scope — see `docs/superpowers/plans/2026-11-07-engine-completion/PLAN.md`).

Reference docs for DAW UX patterns: Pro Tools shortcuts (nudge/trim/clip-by-keyboard),
Cubase/Pro Tools crossfade docs (equal-power vs equal-gain), Izotope/SoundBridge dither
checklist (dither once on bit-depth reduction), EBU R128 / BS.1770 loudness, REAPER/FL
autosave/backup practice, Tracktion Engine UndoManager + Ardour command stack (undo).

---

## 1. Undo/redo depth

**Current state (verified):** `engine/include/nc/engine/UndoStack.hpp` is a correct
multi-level snapshot stack (cap 100, redo cleared on new edit) with tests in
`tests/test_clip_edit.cpp`, but `app/Source/ArrangeView.h` still keeps a single
`undoSnapshot`/`hasUndo` (1 level, no redo) and `MainComponent.cpp:1260` reports
"Redo is not wired". `AutomationLaneView` has its own local 100-deep vector stack.

**Best practice:** Pro Tools / Cubase / Bitwig / REAPER keep deep per-document
history (100+), grouped/coalesced gestures (a drag = one entry), and redo on
Ctrl+Shift+Z / Ctrl+Y. Snapshots of the whole clip list are acceptable at this
scale (≤1024 clips); a full command pattern (TE UndoManager) is overkill until
MIDI-note granularity matters.

**Decision:** wire the existing engine stack into ArrangeView as
`UndoStack<Snapshot{clips, selected}>`; configurable depth (default 100);
drag = one push at gesture start (already the contract in the header).
Add `setMaxDepth` to the engine so the UI can expose it later.

## 2. Router keyboard clip editing ("router" = key routing/focus handling)

**Current state:** `MainComponent::keyPressed` handles space/Esc/Home/Cmd+Z/S/B/E/D,
Delete, tool keys V/T/F/G/X/E, zoom, M/S — but zero clip-focused keys (UI-REVIEW.md
"Remaining: no full keyboard navigation of the timeline"). ArrangeView has no
keyPressed at all; all clip ops are pointer drags.

**Best practice (Pro Tools / Cubase / Logic):** Tab cycles clips; Left/Right
nudges by nudge value; Shift+arrows trims edges; Up/Down moves between tracks;
non-destructive, undoable, one undo per key-burst is acceptable.

**Decision:** ArrangeView gains `selectNext/PrevClip`, `nudgeSelected`,
`trimSelectedLeft/Right`, `moveSelectedTrack`, `cycleCompTake`,
`applyCrossfadeSelected`. MainComponent routes keys only when the arrange view
is visible and no text field has focus (JUCE's focus chain handles the latter).

## 3. Crossfades / comping

**Current state:** per-clip linear `fadeInBars`/`fadeOutBars` via `clipGainAt`
(linear ramps). No crossfade curve choice, no takes/lanes. `ClipState` has no
comp fields; schema v8.

**Best practice:** Pro Tools/Cubase offer equal-power (sin/cos, constant loudness)
and equal-gain (linear) crossfades; comping is a non-destructive take list per
lane with a per-region active-take selector — playback only renders the active
take, originals are never destroyed.

**Decision:** add a JUCE-free `Comping.hpp` (equal-power/linear gains, take list,
overlap math), persist `crossfadeBars` + `compTake` in `ClipState` (schema v9,
old files default 0/0), equal-power rendering in `clipGainAt`'s path via a new
`clipGainCrossfadeAt`. Comp playback: active take is metadata until takes are
stacked in the UI; `cycleCompTake` is the first UI step. No destructive bounce.

## 4. Export variants (stems / MP3 / FLAC / dither / LUFS)

**Current state:** `AudioEngine::renderToWav` only — WAV, session rate, no
dither, no loudness report. MainComponent File > Export Mixdown is WAV-only.

**Best practice:** iZotope/SoundBridge: TPDF dither, applied once, only when
reducing bit depth (32f → 16/24). Stems = one file per track, same range/rate,
dithered like the mix. Loudness per ITU-R BS.1770 (K-weighting, 400 ms blocks,
75% overlap, absolute −70 / relative −10 LU gating) + true-peak. Streaming
targets: −14 LUFS / −1 dBTP; broadcast: −23 LUFS (EBU R128).

**Decision:** new `engine/.../Export.hpp` (JUCE-free): ExportOptions, TPDF dither
(seeded, deterministic), BS.1770 integrated LUFS + true-peak (4× oversampled
peak), stem filename sanitising. App: `renderToFile` dispatches WAV/FLAC/MP3
via JUCE format writers, renders to memory (two-pass: measure → optional
normalise → dither → write), `exportStems` solos one track at a time and
restores mute state. Keep `renderToWav` as a thin wrapper (bitDepth 24, dither).
Sample-rate conversion stays linear-interp (noted limitation; proper SRC was
scoped in the engine-completion plan Task 5 and is out of this gap).

## 5. Autosave

**Current state:** `ProjectFile.cpp` already does atomic write
(`.saving` + `replaceFileIn`) — good. No autosave, no crash recovery, no backup
rotation. ROADMAP Phase 6 deferred it.

**Best practice (REAPER/FL/DaVinci):** journal-style autosave, default 1–5 min,
separate `.autosave` sibling (never overwrites the real project), keep N rotating
backups, crash-recovery prompt on next launch. Manual save stays authoritative.

**Decision:** new `Autosave.hpp` policy (interval/debounce/maxBackups, naming,
prune rule). MainComponent: timer-driven `.autosave` write when the undo depth
has advanced since the last autosave (dirty proxy, documented) and the interval
has elapsed. Full dirty-tracking (per-edit flag) and the recovery prompt are
follow-ups.

## 6. Collab production

**Current state:** `collab/` is a LAN dev server (`ws://`, host console is a
simulator, `HostBackend` contract unimplemented in the native app). Already
production-adjacent: 8-char codes, per-device approval, resume tokens, per-IP
rate limits, Host/Origin allowlists, heartbeat, takeover semantics.

**Production gaps:** no TLS story in-repo, no env-driven allowlists, no
container, no version endpoint, sessions are in-memory (lost on restart).

**Decision:** env-driven `ALLOWED_HOSTS`/`ALLOWED_ORIGINS`/`ALLOW_MISSING_ORIGIN`
in `collab/src/server/index.ts`, `/api/version`, `Dockerfile` (node:22-alpine,
non-root, healthcheck), `Caddyfile` (TLS termination + reverse proxy), and
`PRODUCTION.md` (deploy checklist, proxy headers, known limits: in-memory
sessions, simulator host — native bridge still open).

## 7. CI matrix

**Current state:** `.github/workflows/ci.yml` is one Windows job, Debug only,
no collab/ui coverage, no Release.

**Decision:** matrix `windows-latest` + `ubuntu-latest` × Debug (Release on
windows) for C++/ctest, plus a `node` job (Node 20/22: collab typecheck+test,
ui typecheck+test+build). Suite uses `cache: true` (vcpkg/Catch2) and Debug/Release
presets where the build supports them.

## Verification

- `cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug && cmake --build build && ctest --test-dir build`
  (nc_tests: new Export/Autosave/Comping suites; project JSON v9 round-trip).
- `cd collab && npm run typecheck && npm test`
- `cd ui && npm run typecheck`
- Manual: keyboard clip edit + undo/redo sweep, export WAV/FLAC/MP3 + stems +
  LUFS readout, autosave file appears after edits, collab `/health` + proxy.

## Open items (not in this pass)

1. Native collab bridge: implement `HostBackend` in the DAW webview/bridge and
   replace the simulator host (collab README already flags this).
2. True sample-rate conversion on export (`juce::Resampler` / `ResamplerAudioSource`).
3. Full dirty tracking + crash-recovery prompt + `.ncsproject` backup rotation UI.
4. Take stacking UI (lanes) — engine `CompLane` is ready, UI is one take per clip.
5. Persistent collab sessions (Redis/file store for relay state across restarts).
