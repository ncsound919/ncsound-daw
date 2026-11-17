#!/usr/bin/env bash
# Build and run the component benchmark (and the engine test suite) on a plain
# Linux box: Kaggle, Colab, or any CI runner. Nothing here needs JUCE.
#
#   tools/kaggle/run_bench.sh              # build + test + bench
#   OUT=/kaggle/working/run.json tools/kaggle/run_bench.sh
#   SKIP_TESTS=1 tools/kaggle/run_bench.sh # bench only
#
# Exit status is non-zero when the test suite fails or the bench cannot run.
set -euo pipefail

repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
build_dir=${BUILD_DIR:-"$repo_root/build-kaggle-linux"}
out_file=${OUT:-"$repo_root/tools/kaggle/last_run.json"}
jobs=${JOBS:-$(nproc 2>/dev/null || echo 2)}

# Kaggle and slim CI images do not always ship ninja. Make files cost a little
# more wall time on a small target set but build the same objects, so fall back
# instead of failing the run on a missing generator.
if [ -z "${GENERATOR:-}" ]; then
    if command -v ninja > /dev/null 2>&1; then
        generator=Ninja
    else
        generator="Unix Makefiles"
    fi
else
    generator=$GENERATOR
fi

echo "== configure ($generator, Release) =="
# third_party/JUCE is a Windows-only shallow clone in this repo, so on Linux the
# root CMakeLists takes its documented "JUCE not found" path and configures only
# nc_engine, nc_tests and nc_bench.
#
# EXTRA_CMAKE_ARGS exists for the offline Kaggle kernel, which passes
# -DCPM_Catch2_SOURCE=<dataset>/catch2 so the build does not need network.
# shellcheck disable=SC2086
cmake -S "$repo_root" -B "$build_dir" \
      -DCMAKE_BUILD_TYPE=Release \
      -G "$generator" \
      ${EXTRA_CMAKE_ARGS:-}

echo "== build =="
cmake --build "$build_dir" --target nc_bench nc_tests -j "$jobs"

if [ "${SKIP_TESTS:-0}" != "1" ]; then
    echo "== engine test suite =="
    # Catch2 exits non-zero on any failed assertion, so this doubles as the
    # correctness gate before the numbers are worth collecting.
    "$build_dir/tests/nc_tests"
fi

echo "== bench =="
"$build_dir/tests/nc_bench" --json "$out_file"
echo "wrote $out_file"
