# Engine component audit: what is shaky, and what to do first

Written after the AudioEngine decomposition (plan 07). Ordering is by risk to
the user, not by size: a component that can glitch or crash playback outranks a
big function that is merely hard to read.

Each entry says what the evidence is, what the failure would look like, and the
smallest change that removes the risk. The benchmark in `tools/kaggle/` covers
the pure-engine kernels; the block path is not covered yet, which is itself the
first gap.

## 1. Streaming cache slot reuse (audio-thread data race)

`AssetLibrary::serviceStreaming` keeps two read-ahead slots and refills
`1 - active` (`app/Source/AssetLibrary.cpp`, the loop around line 146). The
audio thread reads the active slot per sample (`readSourceSample`), reloading
the index each call.

There is no block stamp on a slot. Two refills in quick succession can reach the
slot a late block is still reading, because "the slot I just refilled" and "the
slot a block captured two publishes ago" are the same slot with only two
buffers. The writes are `std::fill` + `reader->read` into memory the reader
thread may be reading: a torn sample, an audible click on a long import, not a
crash.

Smallest fix: give each slot a publish stamp from the transport block counter
and skip a refill whose target slot was published fewer than two blocks ago,
unless no callback is running. Triple buffering alone narrows the window without
closing it.

Why this is first: it is the only race found that a normal user can hear (import
a long file, play from the middle) and it has no test.

## 2. Intermittent heap corruption in `NCMixRackTests` (0xC0000374)

Seen occasionally during earlier phases; never reproduced on demand. It predates
the decomposition and is not tied to a known test. A heap corruption report with
no reproducer is a false-negative machine: it makes every later green run
slightly untrustworthy.

What was added already: `SnapshotReclaimTests` now drives the block path on one
thread while publishing tempo maps, automation lanes and MIDI clips on another,
then requires the retained lists to drain to zero. Writing it reproduced an
access violation on the first attempt, which is the useful part.

**The contract it exposed:** `reclaimRetiredSnapshots` uses `deviceOpen` as the
stand-in for "a callback may be reading". With the device suspended it frees
every retired snapshot immediately — correct for a real device, lethal for any
harness that renders on its own thread and polls on the message thread, which is
exactly what `processBlockForTesting` invites. The test now documents this and
does not poll mid-flight.

This may or may not be the intermittent 0xC0000374: the crash here is
`0xC0000005`, reproduced on demand, not the 0xC0000374 abort. Treat them as
separate until proven otherwise.

Next step: make the "is a callback running" question answerable without
`deviceOpen` (a dedicated counter that `processBlockForTesting` also bumps), so
the rule is enforced by the engine rather than by a comment in a test. After
that, enable the Windows heap verifier (`gflags /p /enable NCMixRackTests.exe
/full`) in a soak run and treat the first hit as the reproducer.

## 3. `renderStripPass` (211 lines, per-sample hot path)

The four-pass split left this one knowingly over the 200-line rule: every route
under it either adds a call for a counter or splits a per-sample loop, and both
change float accumulation order, which the digest pins.

It is the largest thing that runs 48000 times a second per block, so it is where
optimisation has to start, and it cannot be touched safely without a block-level
benchmark to prove a change is faster rather than merely different. Readability
is secondary here; the constraint is the digest.

## 4. `renderToFile` (254 lines) and `exportStems` (180)

Offline paths, so a slow run costs wall time, not dropouts. They are the largest
functions left in the project and they mix three concerns (range planning,
per-track soloing, file writing).

Fix shape: split the per-stem loop from the mix/write body; exportStems then
reuses renderToFile's writer instead of duplicating the format handling. Not
urgent, and only after the block path has a benchmark.

## 5. Hosted insert lifetime (`HostedInsert`, `TrackInsertRack`)

Inserts are retired through `TrackInsertRack::collectGarbage`, polled from
`AudioEngine::pollRetiredInserts`. The rule is the same block-counter rule as the
snapshot lists, but implemented separately, so the two can drift (the plan's own
risk list flagged this).

Fix shape: express both in terms of one `retireUntilSafe(list, blockNow,
callbackRunning)` helper, so there is one place where "safe to free" is decided.

## 6. `AutomationLanes` publication

`setTrackAutomation` allocates a lane, retires the old one and republishes. Same
pattern as tempo and MIDI clips, now with its state in `AudioEngineAutomation.h`.
Not shaky today, but it is the component most often published from UI code, so it
is the one that would expose a mistake in the retirement rule first.

## What is deliberately not on this list

- **`AudioEngine.h` size.** 695 lines with the callback state in three
  declaration headers. Further splitting is cosmetic and the last split already
  cost more diff than it saved.
- **`TransportClock`/`AssetLibrary` API shape.** Both take what they need as
  parameters and neither allocates on the audio thread; there is nothing to fix.

## Order of work

1. Block-path benchmark (Windows, no device) so any change below can be measured.
2. Streaming slot stamping (audit item 1) with a test that plays a streamed
   asset across a refill.
3. Heap verifier soak (item 2) — gates all optimisation, because a corruption
   bug invalidates every measurement.
4. Then the measured items: `renderStripPass` inner loop, loudness/dither in the
   export path, MIDI scheduling if it shows up hot.
