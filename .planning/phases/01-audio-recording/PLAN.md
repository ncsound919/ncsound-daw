# Phase 1 — Audio Recording

Goal: a take recorded from the React transport lands on the armed track as an undoable clip.

## What already exists (verified by reading the code)

- `AudioEngine::startRecording (folder, error)` — requires an open device and at least one armed
  track; streams each armed track to its own WAV through a writer thread.
- `AudioEngine::stopRecording (folder, 24, error)` → `std::vector<RecordedTake>` with
  `{track, file, startFrame, frames}`.
- `MainComponent::startRecording()` / `finishRecording()` — punch-in from the playhead, starts
  playback, converts each take's `startFrame` to a bar and calls `arrange.placeRecordedTake`.
- Punch-out at the loop end is flagged by the audio thread; `finishRecording` places takes.

## Gap (why the button was dead)

The web bridge had 48 routes and none for recording, so `Transport` rendered its Record button
with `disabled={!onRecord}` and the tooltip "Recording is not available". The engine work was all
there; nothing called it.

## Tasks

1. **Project recording state** — `buildTransport()` sets `recording` from
   `engine.isRecording() || engine.isMidiRecording()`. (done)
2. **Two routes, reusing the native steps** — `startRecording` → `startRecording()`,
   `stopRecording` → `finishRecording()`, each returning `buildState()`. Reusing the native
   methods keeps placement, latency trim and status text in one place. (done)
3. **Regenerate the route contract** — `node scripts/gen-native-routes.mjs` → 50 routes. (done)
4. **Bridge + types** — `startRecording()` / `stopRecording()` on the client; `recording?: boolean`
   on `TransportState`. (done)
5. **Shell wiring** — `WorkspaceProps.onRecord`, passed to `Transport` with
   `recording={transport.recording}`; `App` calls the matching bridge method. (done)
6. **Tests** — Record enabled and calls back with `true` when wired; stays disabled when no route
   is wired. (done, surfaces.test.tsx)

## Verification

```
cd ui
node node_modules/typescript/bin/tsc -b --noEmit        # expect 0
node node_modules/vitest/vitest.mjs run                 # expect all pass
node node_modules/vite/bin/vite.js build                # expect exit 0
cd ..
build\_ncbuild_ok.cmd                                   # vcvars + cmake (app, tests, probe)
ctest --test-dir build -C Release --output-on-failure    # expect 168/168 (mix_rack included)
```

Desktop check: arm a track (R), press Record, confirm the button shows the recording state and the
status strip says `RECORDING / ARMED TRACKS CAPTURING`; press Record again and confirm
`TAKES PLACED / n OF n` plus a new clip with peaks on the armed track.

## Risks / notes

- Without an input device or with no armed track the engine returns a real error; the strip must
  show it rather than fail silently (this is the same class of bug that hid the insert failures).
- `finishRecording` reads from the current working directory (`Recorded Takes/`); an installed
  build should move this next to the project file. Tracked as a follow-up, not a Phase 1 blocker.

## Exit criteria

Record → stop produces a placed, undoable clip and the transport reflects capture state.
