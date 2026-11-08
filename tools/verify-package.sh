#!/usr/bin/env bash
#
# Asserts what a CPack package actually contains before it can be attached to a
# release. Each check here is one that was done by hand and then forgotten.
#
#   tools/verify-package.sh build/_pkg/ncsound-daw-0.1.0-AMD64       # package tree
#   tools/verify-package.sh build/_pkg/ncsound-daw-0.1.0-AMD64.zip   # or the archive
#
# Exit 0 when every check passes, 1 when the package is wrong, 2 on a usage or
# IO problem. One line per check, so a CI log shows what it looked at.

set -euo pipefail

usage() {
    echo "usage: $0 <package-directory|package.zip>" >&2
    exit 2
}

[ "$#" -eq 1 ] || usage

input=$1
[ -e "$input" ] || { echo "not found: $input" >&2; exit 2; }

tmp=""
cleanup() { [ -n "$tmp" ] && rm -rf "$tmp"; }
trap cleanup EXIT

if [ -d "$input" ]; then
    root=$input
elif [ -f "$input" ]; then
    case "$input" in
        *.zip|*.ZIP) ;;
        *) echo "not a directory or .zip: $input" >&2; exit 2 ;;
    esac

    tmp=$(mktemp -d)
    unzip -q "$input" -d "$tmp"
    root=$(find "$tmp" -mindepth 1 -maxdepth 1 -type d -print -quit)

    [ -n "$root" ] || { echo "archive has no top-level directory: $input" >&2; exit 2; }
else
    echo "not a directory or file: $input" >&2
    exit 2
fi

echo "verifying $root"

fail=0

required() {
    if [ -e "$root/$1" ]; then
        printf '  ok       %s\n' "$1"
    else
        printf '  MISSING  %s\n' "$1"
        fail=1
    fi
}

forbidden() {
    local hit
    hit=$(find "$root" -iname "$1" -print -quit)

    if [ -n "$hit" ]; then
        printf '  UNWANTED %s (%s)\n' "$1" "${hit#"$root"/}"
        fail=1
    else
        printf '  ok       no %s\n' "$1"
    fi
}

# Both plug-ins as whole bundles: a host needs the directory, and the binary
# inside Contents/x86_64-win is what actually loads, so both are asserted.
# moduleinfo.json lives under Contents/Resources in JUCE's VST3 layout.
for bundle in "Chop Station" "808 Designer"; do
    required "VST3/$bundle.vst3/Contents/x86_64-win/$bundle.vst3"
    required "VST3/$bundle.vst3/Contents/Resources/moduleinfo.json"
done

# The linker's side products sit next to the bundle in the build tree; the
# install rules exclude them, and a regression there ships a .lib with the
# product.
forbidden '*.exp'
forbidden '*.lib'
forbidden '*.pdb'
forbidden '*.ilk'
forbidden 'juceaide*'

if [ -n "$(find "$root" -type d -iname 'include' -print -quit)" ]; then
    printf '  UNWANTED include/ directory\n'
    fail=1
else
    printf '  ok       no include/ directory\n'
fi

if [ "$fail" -ne 0 ]; then
    echo "FAILED: $root"
    exit 1
fi

echo "PASSED: $root"
