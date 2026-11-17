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

## Running it on Kaggle (no git remote required)

This tree has no remote, so the notebook does not clone: it reads the sources
from a private Kaggle dataset. `nc_bench.ipynb` and `kernel-metadata.json` here
are the pushed kernel; `dataset-metadata.json` describes the dataset.

```bash
# 1. Dataset: the repo tree, with third_party/JUCE removed (so CMake takes its
#    documented "JUCE not found" path) and third_party/catch2 vendored, so the
#    kernel needs no internet.
python -m kaggle datasets create -p <dir-with-repo-and-dataset-metadata> --dir-mode zip
python -m kaggle datasets status <owner>/<dataset>          # wait for "ready"

# 2. Kernel: directory holding nc_bench.ipynb + kernel-metadata.json.
python -m kaggle kernels push -p tools/kaggle
python -m kaggle kernels status <owner>/ncsound-daw-engine-verify
python -m kaggle kernels output <owner>/ncsound-daw-engine-verify -p out
```

`--dir-mode zip` uploads the *contents* of the directory, and Kaggle flattens
them into the dataset root. Kaggle has also moved dataset mounts between
`/kaggle/input/<slug>` and `/kaggle/input/datasets/<owner>/<slug>`, so the
notebook resolves the source root by looking for the repo's `CMakeLists.txt`
instead of hard-coding a path.

The kernel needs `csv` and the JUnit reporter only for its own summary: the
gating run is `run_bench.sh`, which fails the cell on any assertion failure.
Outputs are `readiness.json` (machine-readable evidence), `junit.xml`,
`nc_bench.csv`, `nc_bench_flagged.csv` and `nc_bench.png`.

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

### Harness 2: what changed, and what the evidence actually shows

Harness 1 accumulated every operation into a `volatile` global inside the timed
loop. On MSVC that store also carries release ordering, so the loop could not be
pipelined and every operation paid for the store as well as the work.

The observable symptom was that the cheap kernels were not separable: in one
harness 1 run, six kernels doing very different work (a binary search, an
8192-iteration range test, an SPSC push/pop, a three-call window plan) all
reported 3.73-3.81 ns/op - within 2% of each other. Values that close across
work that different point at a shared cost in the harness, not at six equal
kernels.

Harness 2 accumulates into a local and folds the result into the sink once per
batch, and adds `harness.baseline_loop` - the same loop shape with trivial work -
so the floor is a measured row rather than an assumption. In harness 2 runs that
calibration reports 1.5-1.8 ns/op and the same kernels spread over 2.5-10 ns.

Two things this does **not** say, both of which the first draft of this section
got wrong:

- It is not true that every cheap kernel was measuring only the harness. In the
  harness 2 run `clip_timeline.clip_active_at` still reports 3.81 ns/op, against
  3.75 before. Its cost was real all along; it was one of the rows that happened
  to sit near the floor.
- The old and new absolute numbers are not comparable. Those runs were different
  Kaggle instances as well as different harnesses, and `export.measure_loudness_10s`
  alone varies 22M-39M ns between instances of the same build.

Read every row against `harness.baseline_loop`:

- a kernel within ~2x of it is still loop-dominated and should be pushed harder
  (more work per timed region) before anyone reads a trend into it;
- a kernel well above it is being measured.

`run.json` carries `"harness": 2`. A baseline captured with harness 1 must not be
compared against harness 2 numbers - recapture instead.

## Baseline and regression gate

`tools/kaggle/baseline.json` is a **Kaggle-native capture**: harness 2, 4
threads, taken on the `ncsound-daw-engine-verify` kernel. Values are absolute
`ns_per_op` for that machine class, which is the only thing the gate compares.

It replaced a harness-1 capture taken on the Windows dev box. That baseline was
unusable for the gate in practice: every Kaggle kernel came out 0.08-0.61x
"faster", which is a statement about two different CPUs and toolchains rather
than about the engine, and the 1.25x threshold could never fire.

The first gate run against the Kaggle-native baseline measured 0.85-1.02x across
all eleven rows, so the reference is sound and the run-to-run spread is a few
percent - comfortably inside the 1.25x threshold. To re-seat the baseline after
an intentional performance change, run the kernel and copy its `run.json` to
`baseline.json`; keep the `"harness"` field honest.

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

`--sections` adds the split of the block's cost by render pass, taken for the
`idle` scenario (before any clip exists) and the `timeline` scenario with the
same method, so the two are comparable:

```bash
NCBenchRender.exe --sections --json sections.json
```

Each set reports `ns_per_block` and `share` for `commands`, `clips`, `inserts`,
`strip`, `master`, plus `strip.dsp` — the cost of one track's
`MixRackChannel::processSample` over the block. `strip.dsp` is a *subset* of
`strip`, not extra cost, and is deliberately excluded from the total. Only track
0 is timed inside the sample loop; clocking every track would cost more than the
call itself.

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
