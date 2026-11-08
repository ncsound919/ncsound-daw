# Dependencies

Two things a clean clone needs beyond the standard toolchain. Both are pinned;
neither needs a network fetch at configure time.

## JUCE

A shallow clone in `third_party/JUCE` (gitignored), pinned in
`third_party/JUCE_PIN.txt`: tag `8.0.17-security`, commit
`dabd5ad13cd869cba41046947fffc8e5ce5cd662`, branch `security-patches/v8`.

The root `CMakeLists.txt` refuses to configure below `NC_JUCE_MIN_VERSION`
(8.0.17) instead of silently building known-vulnerable code, and prints the
exact commands to restore the pin. Populating it is the one manual step a fresh
checkout needs:

```bash
git -C third_party/JUCE fetch --depth 1 origin \
    refs/tags/8.0.17-security:refs/tags/8.0.17-security
git -C third_party/JUCE checkout 8.0.17-security
```

## ChordStudio mix-rack sources

`nc_mixrack` compiles seven translation units from ChordStudio (Mixology chain
resolution and presets, the Open Mic mic-preamp saturation/oversampling stages),
plus headers from Overlay FX. It is not a header-only or package-managed
dependency.

**Why it is vendored.** The copy on this machine is an extracted snapshot with no
`.git` and there is no upstream remote recorded anywhere in it, so there is no
commit to pin with `FetchContent` or a submodule. Vendoring the exact closure and
locking it by sha256 is the closest available equivalent: two clones either match
the lock or fail configure, which is what a commit pin buys.

**Shape of the copy.** 25 files, ~165 KB, under `third_party/chordstudio/`,
preserving their ChordStudio-relative paths so `app/CMakeLists.txt` reads the
same either way. `third_party/chordstudio/lock.cmake` holds a sha256 per file.

**Lookup order** (`cmake/ChordStudioDeps.cmake`):

| Order | Source | Hash-checked |
|-------|--------|--------------|
| 1 | `-DNC_PLUGINS_ROOT=<path>` | no |
| 2 | `NC_PLUGINS_ROOT` environment variable | no |
| 3 | `third_party/chordstudio` | yes |

The vendored copy is the default, so a clean clone builds with no sibling
checkout. Overrides exist for working against a live ChordStudio tree, and those
are deliberately not hash-checked because they are expected to move.

**Refreshing.** Adopting newer ChordStudio sources is one command:

```bash
python tools/vendor-chordstudio.py ../ChordStudio-master
```

It re-reads the source list from `app/CMakeLists.txt` (so a new `.cpp` cannot be
compiled without being copied), rewrites `lock.cmake` and `PROVENANCE.md`, and
prints what was added, changed and removed. Commit `third_party/chordstudio/`
and `lock.cmake` in the same commit.

**Never edit the vendored files in place** — the lock will fail the next
configure, and the change belongs upstream.

## WebView2 (hub UI)

`app/CMakeLists.txt` builds the hub with `juce_add_gui_app(... NEEDS_WEBVIEW2 TRUE)`,
so configure needs the WebView2 NuGet package. JUCE's `FindWebView2.cmake` looks in
the local NuGet folder and, failing that, in `JUCE_WEBVIEW2_PACKAGE_LOCATION`.

On this machine the package sits outside the repository and its path exists only
inside `build/CMakeCache.txt`, so **a fresh build directory fails configure** with
`Could NOT find WebView2 (missing: WebView2_include_dir WebView2_library)`.
Supply it explicitly:

```bash
cmake -S . -B build -DJUCE_WEBVIEW2_PACKAGE_LOCATION=C:/Users/Asus/Desktop/Plugins/.deps/WebView2
```

or install the package into the NuGet folder as JUCE's own message describes.
This is the last hand-set cache value the build depends on; until it is supplied
by CI or replaced with a committed dependency, a clean clone cannot configure the
hub. Tracked as phase 2 work in the release plan.

## Licence consequence

The ChordStudio snapshot is **GPL-3.0**. `nc_mixrack` links it statically into
the app and the plug-ins, so those binaries are derivatives and GPL-3.0 applies
to them whatever was decided about JUCE. Vendoring does not create that
obligation — statically linking already did — but it does make the source
availability requirement easy to satisfy. This needs an owner decision before
public distribution; see phase 3 of
`docs/superpowers/plans/2026-11-08-release-completion/PLAN.md`.
