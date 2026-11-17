# Benching and e2e runs on Kaggle compute

The goal is a repeatable, comparable number per engine kernel, on hardware that
is not the developer's laptop, so a regression is visible before it reaches a
driver callback.

## What runs where

| Surface | Buildable on Kaggle (Linux) | Why |
| --- | --- | --- |
| `nc_engine` + `nc_tests` | yes | pure C++, no JUCE |
| `nc_bench` (this harness) | yes | pure C++, no Catch2, no JUCE |
| `NCMixRackTests`, `NCSoundHub` | no | JUCE, WebView2, and a Windows device stack |

The root `CMakeLists.txt` skips every JUCE target when `third_party/JUCE` is
absent, so a Linux configure is a supported path rather than a workaround.

## The numbers Kaggle cannot give you

Kaggle's CPU is a shared cloud vCPU, not the machine that has to hold a 10 ms
WASAPI block. Treat it as **relative** data: the same kernel before and after a
change, on the same machine class. It cannot tell you whether playback drops
out on the user's box, and it has no `AudioIODevice` at all, so every number
here is kernel-level.

What it can do, and the reason to bother:

- catch a kernel that got 3x slower in a refactor, which the digest and the
  test suite cannot see (both pass on slow code);
- give a second, independent toolchain (clang/libstdc++ vs MSVC/STL) for the
  pure engine, which is where the transformation and parsing maths lives;
- run the engine's correctness suite for hours without competing with the DAW.

## Running it

In a Kaggle notebook, or on any Linux box with cmake and a compiler:

```bash
git clone <this repo> repo && cd repo
bash tools/kaggle/run_bench.sh
```

That configures `build-kaggle-linux/`, builds `nc_bench` and `nc_tests`, runs
the whole engine suite, then writes `tools/kaggle/last_run.json`.

Environment knobs: `BUILD_DIR`, `OUT`, `JOBS`, `SKIP_TESTS=1`.

Result shape (`nc-bench/1`):

```json
{
  "schema": "nc-bench/1",
  "run": { "utc": "...", "hardware_threads": 4, "passes": 3 },
  "kernels": [
    { "name": "tempo_map.ppq_at_samples", "units": "query",
      "ops": 4096, "ns_per_op": 44.219, "ns_total": 45280100.0 }
  ]
}
```

Each kernel does a warm-up pass and then three timed passes; the fastest is
reported. Compare `ns_per_op` between runs of the same kernel name on the same
machine class — not across kernels, since they measure different units.

## Baseline and regression gate

`tools/kaggle/baseline.json` is the current reference, captured on this
project's Windows dev box (MSVC Release, 4 threads). It exists to make the
notebook's comparison step meaningful between two Kaggle runs, not to compare
Windows against Linux.

The notebook flags a kernel when `ns_per_op` grows past 1.25x baseline, and
prints the deltas sorted worst-first. A flagged kernel is a question, not a
verdict: `resample.bank_build` and `export.measure_loudness_10s` are
message-thread / offline paths where a slow run is not a real-time risk, while
`spsc_queue.push_pop` and `resample.weights_lookup` sit on the audio thread and
are budget-critical.

## Adding a kernel

Add a block in `tests/bench_components.cpp` using `measure (name, units, ops,
iterations, fn)`, keep `ops` the number of logical operations per call so
`ns_per_op` stays comparable, and give it a stable name. Do not rename existing
kernels: the baseline is keyed by name.

## The block path (Windows only)

`NCBenchRender` (built from `app/`, not `tests/`) drives the real
`renderBlock` with no audio device, at 64/256/512 frames, in two scenarios:
`idle` (transport running, nothing to render) and `timeline` (demo assets on
every track that has one, 8 clips each). It emits the same `nc-bench/1` schema
plus a `realtime_factor` per row, where 1.0 means the block consumed its whole
budget.

```bash
cmake --build build --target NCBenchRender
build/app/NCBenchRender_artefacts/Release/NCBenchRender.exe --json out.json
```

`tools/kaggle/render_baseline.json` is the current reference: 44.1 kHz,
16 tracks, 128 clips.

```
render_block.idle.512        5.94 ms   1.95x realtime
render_block.timeline.512   12.12 ms   0.96x realtime
render_block.timeline.64     1.45 ms   1.00x realtime
```

Idle at 512 frames costing 5.9 ms means most of the block is per-track work
that runs whether or not anything is playing. That is the first thing an
optimisation pass should look at, and it is why the idle row exists.
