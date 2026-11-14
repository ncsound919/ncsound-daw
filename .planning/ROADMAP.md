# NCSound DAW — Roadmap to Fully Working

Status date: 2026-11-12, revised after the DAW-01..DAW-13 audit close-out. Built from an audit of the running app and the code, not from the
2026-11-07 parity doc (which is now out of date: the React shell owns the whole window, tools,
undo, mixdown, sessions and the 8-slot rack all ship).

Legend: **DONE** = verified in the running app · **BLOCKED** = cannot complete without hardware
or a user-run dialog · **OPEN** = not implemented.

## Where the app stands

Verified today: single React shell (no JUCE header), 8 insert slots that load real VST3s,
Channel view faceplate with PARAMS toggle, per-track/clip editing with undo, mixer with master
seam, session/export routes, piano roll, automation lanes, 168/168 native tests
(`ctest --test-dir build -C Release`), 81/81 UI tests (`vitest run`).

## Phases

### Phase 1 — Audio recording (DONE; hardware pass outstanding)
The engine records (`startRecording`, `stopRecording`, `isRecording`, punch-out and
`RecordedTake`), the bridge exposes both routes, and the transport's Record button calls them — it
is disabled only when a host has not wired `onRecord`, which this app does. Takes land as clips
through `ArrangeView::placeRecordedTake`, so they get waveform peaks and an undo step.

Evidence: four `[record]` native tests (armed track captures input, unarmed start refused, input
latency trimmed, two armed tracks produce two takes) and two UI tests (the button enables and calls
the route when wired; it stays disabled when not). All pass in the 168/168 run.

Remaining: a desktop capture with a real input device — arm a track, record 3 s, confirm the take
lands with peaks. That needs hardware, not code.

### Phase 2 — Insert and routing completion (OPEN)
Bypass (alt-click), plug-in editor window, drag-reorder, master A/B capture/recall, AUTO toggle.
Engine already holds `bypassed` in `PluginSlotState` and `moveTrackInsert`.

Exit criteria: bypass flips audio and shows in the strip; editor opens for slot instruments; A/B
recalls a master snapshot.

### Phase 3 — Catalogue reliability (OPEN, root cause found)
The startup VST3 scan parks (27% for minutes, 21 → 122 plug-ins over ~2 min) and the browser
reports rows as Missing while it runs. Inserts now load via on-demand resolution, but the scan
itself needs a real fix: per-bundle timeout, progress that reflects work done, and a resumable
index so a launch does not rescan 25 bundles.

Exit criteria: cold start indexes the local folder in one pass, no row is ever permanently
Missing, and a second launch reuses the cached index.

### Phase 4 — Mixer fit at 16 tracks (OPEN, cosmetic)
112 px strips plus a 10 px master seam stop the overlap, but 16 strips still scroll. Add a
density step (compact meter/fader) so 16 fit at 1280 px, or a "fit strips" toggle.

### Phase 5 — Session/export proof (BLOCKED on dialogs)
Routes exist (`openSession`, `saveSession`, `saveSessionAs`, `mixdownWav`, `collectMedia`) but no
test writes a real file. Needs a user-driven run with the file chooser, then a round-trip check:
save → quit → open → identical state.

### Phase 6 — Performance and packaging (OPEN)
Soak the audio thread with the two suite VST3s for 30 min, keep pluginval strictness 5 in the gate,
and produce an installer that drops the app plus the per-user VST3 folder.

## Order

1 → 2 → 3 → 4 → 5 → 6. Phase 1 first because a DAW that cannot record is not working; Phase 3
next because it degrades every load path; Phase 4 is cosmetic and can ride along.
