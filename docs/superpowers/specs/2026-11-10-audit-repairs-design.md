# DAW Audit Repairs Design

**Intent:** Repair every actionable bug in the supplied C++ engine, JUCE host, React UI, and bridge/schema audit, preventing audio-thread data races, dropped/corrupt session state, incorrect MIDI scheduling, UI state loss, and contract mismatches.

**Goals:** Keep each repair targeted to the existing implementation; add focused regression coverage for deterministic behaviors; preserve unrelated user changes already present in the working tree; verify each subsystem using its existing test/build commands.

**Non-goals:** Broad architectural rewrites, cleanup of verified non-issues, committing or publishing changes, and changes outside the reported bugs.

**Constraints:** Retain existing C++/JUCE/React conventions and dependencies. Do not overwrite the existing uncommitted work. Report runtime concurrency checks that cannot be reliably tested as limitations rather than claiming them covered.

## Approaches

1. **Targeted in-place fixes (selected):** Apply the report's minimal fixes in four subsystem batches, with regression tests where a deterministic seam exists. This minimizes overlap and regression risk while fixing the specified issues.
2. **Broader redesign:** Replace shared state and lifecycle mechanisms with new concurrency/state architectures across the engine and UI. This may prevent related bug classes but expands risk and would conflict more heavily with current in-progress work.

The targeted approach was selected because the report provides concrete repairs and the repository already contains unrelated in-progress changes in affected files.

## Work batches

1. **Engine:** Fix the remaining `AnalogCharacter` settings race, resample pre-publication access, MIDI block-continuity and sample-offset boundary behavior, comp-take modulus, trim ceiling, bar/beat overflow, JSON Unicode escape decoding, AudioSuite parameter overflow, and zero-window voice conversion. Add tests for these deterministic cases, including master-insert description XML if not already covered.
2. **JUCE host:** Fix recording buffer sizing and writer-thread startup; protect threaded-take publication/lifecycle; guard master rack processing, swap AUX DSP state safely, fix streamed source-offset windows, move voice conversion off the UI thread with safe result/error/lifetime delivery, remove audio callback allocations, and correct slot persistence/access, denormal, legacy synth-state, and master format issues. Add practical host tests where existing test seams permit.
3. **React UI:** Fix duplicate piano-roll context deletion, stale state/response ordering, CSS fader positions, mid-gesture draft resets, modal keyboard handling, non-passive wheel zoom/reciprocity, automation undo, dead channel parameter controls, optional peak data, plugin row keys, loop bounds, stale fit closure, late JUCE event subscriptions, faceplate inert state, and inaccurate shortcut hints. Extend focused UI tests for the regressions.
4. **Bridge/schema:** Publish/edit track width, connect MIDI learn, clarify or align the schema with the actual ProjectDocument format, reconcile/document numeric ranges, omit MIDI asset IDs, align clip index/dirty contracts, harden route-generation extraction, update stale slot comments, and normalize/document vocabulary casing as appropriate. Verify generated-route parity and project tests.

**Implementation note:** Exact behavior will follow each audit finding's proposed repair except where current source/tests show that the proposed change would be incorrect; any such divergence will be explained before proceeding past that finding.
