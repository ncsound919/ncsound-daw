# Plan — Release completion (validated, reproducible, legally shippable)

**Status:** Plan for approval. No implementation started beyond what is noted as landed.
**Date:** 2026-11-08
**Research:** [`../../research/2026-11-08-release-readiness-research.md`](../../research/2026-11-08-release-readiness-research.md)
**Baseline:** 160/160 ctest, UI 43/43 + lint 0 errors, `cpack -G ZIP` produces a
verified 14 MB archive. Those are the three things that already hold.

The workflow this follows is research → plan → execute → verify → record. The
`gsd-sdk` / `.planning/` toolchain is not installed in this repository, so the
gates below are run by hand and evidence is appended to `DAW_WORK_LOG.md`; see
§9 for how each gate is discharged.

---

## 0. What "completion" means here

Completion is **not** "all axes 10/10". Use and Code quality can be measured and
are close. Style and Innovation are owner/product judgements that no patch
settles. Completion means:

1. A stranger can install it, and a host can load it, on a clean machine.
2. CI reproduces every byte of the release from a clean clone.
3. Nothing in the shipped artifact is legally exposed.
4. The code can absorb the next feature without a 3,000-line file fight.

**Exit criteria for 1.0:**
- [ ] Clean-clone CI builds app + both plug-ins and packages them, with no path
      outside the repository.
- [ ] pluginval strictness 5 green for **both** VST3s, plus acceptance renders.
- [ ] Signed installer installs to the standard VST3 location on a clean VM,
      uninstalls without orphans, and both plug-ins scan and load in two hosts.
- [ ] JUCE and font licensing decided in writing, with `LICENSE`,
      `THIRD-PARTY-NOTICES.md` and a root `README.md` present.
- [ ] 150% DPI verified in three hosts, no clipping and no double-scaling.
- [ ] No source file above the size budget in §4.

---

## 1. Phases, in dependency order

| # | Phase | Blocks | Can an agent finish it alone? |
|---|-------|--------|-------------------------------|
| 1 | Dependency determinism | everything | yes |
| 2 | Validation matrix | release | yes |
| 3 | Legal/IP decisions | distribution | no — owner decides, agent implements |
| 4 | Structural refactor | velocity | yes, in slices |
| 5 | UI at 150% + accessibility | release | mostly — needs a real display/host |
| 6 | Installer + signing | release | partly — needs a certificate |
| 7 | Operational readiness | post-1.0 | partly |

Phase 1 comes first because every later gate ("CI builds it", "clean VM") is
meaningless while the app build reads `../../ChordStudio-master`.

---

## 2. Phase 1 — Dependency determinism

`app/CMakeLists.txt:4` hardcodes `set(NC_PLUGINS_ROOT ".../../../ChordStudio-master")`.
CI cannot reproduce the app, and a fresh clone cannot configure it.

**Tasks**
1.1 **Done.** Neither pinned route in the original draft was possible: the
    ChordStudio copy has no `.git` and no recorded remote, so there is no commit
    for `FetchContent` or a submodule to point at. The include closure is 25
    files / 165 KB once walked from both sides of the interface, so it is vendored at `third_party/chordstudio/` with a sha256
    `lock.cmake` -- the nearest available equivalent to a commit pin. Resolution
    and verification live in `cmake/ChordStudioDeps.cmake`.
1.2 **Done.** A missing or partial copy fails configure with the exact missing
    files and the three ways to supply them, instead of half-configuring.
1.3 **Done.** `docs/DEPENDENCIES.md` records the pin, the lookup order, the
    refresh command and the GPL-3.0 consequence.
1.4 **Done.** JUCE was already a pinned shallow clone
    (`third_party/JUCE_PIN.txt`: `8.0.17-security`, commit `dabd5ad`), now also
    recorded in that document.
1.5 **Done.** CI supplies its own dependencies instead of inheriting them.
    `tools/fetch-native-deps.ps1` clones JUCE shallowly at the tag in
    `third_party/JUCE_PIN.txt`, fails if `HEAD` is not the pinned commit, and
    downloads the pinned WebView2 package; both native jobs then export
    `NC_WEBVIEW2_PACKAGE_LOCATION`. Before this the `native-windows` job reported
    success while building only the engine, because `third_party/JUCE` is
    gitignored and the top-level CMakeLists skips the app and both plug-ins when
    it is absent.

**Verification**
```bash
git clone <repo> /tmp/clean && cd /tmp/clean
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release   # must configure with no sibling checkout
cmake --build build --target NCChopStation_VST3 NCEightOHeight_VST3
grep -rn '\.\./\.\./' CMakeLists.txt app/CMakeLists.txt   # must be empty
```
**Exit criteria:** a configure with no sibling checkout resolves the vendored
copy and builds both plug-ins; the copy is hash-locked; no absolute or sibling
path remains.

**Landed 2026-11-08.** Configure reports the vendored root, `nc_mixrack` and
`NCMixRackTests` build from it, `ctest -R mix_rack` passes, and a one-byte edit
to a vendored file fails configure on the hash. Not verified locally: a compile of the
*plug-ins* from a clean tree - the JUCE clone and the app build are now wired into
CI (1.5) but the first runner execution is their real test. A fresh build directory also exposed a
second unpinned dependency, since fixed: WebView2's package path existed only in
`build/CMakeCache.txt`, so `juce_add_gui_app` could not configure from scratch.
`cmake/WebView2Deps.cmake` now resolves it from a documented environment variable
and a fresh directory configures without cache edits (243.8 s, exit 0).

---

## 3. Phase 2 — Validation matrix

Today `pluginval` covers Chop Station only, at strictness 5 with
`--skip-gui-tests`; the 808 is unvalidated and nothing guards DSP output.

**Tasks**
3.1 Add the 808 to the existing job (`plugins:` takes one path per line).
3.2 Add a nightly job at strictness 10. If the 808 uses
    `juce::AudioParameterBool`, set
    `DISABLED_TESTS: Discrete parameter state restoration` (documented false
    positive) and record why in the workflow file.
3.3 Add acceptance renders: one config per plug-in, reference checked in,
    `Tracktion/pluginval/acceptance@v2`.
3.4 Add a package-content assertion script (`tools/verify-package.sh`) so the
    checks done by hand today are enforced: both `.vst3` binaries, both
    `moduleinfo.json`, no `*.exp/*.lib/*.pdb`, no `juceaide`, no `include/`.
3.5 `release.yml`: tag → Release build → `cpack` → `verify-package.sh` → upload
    artifact → attach to the GitHub release.

**Verification:** the three jobs pass on a tag; `verify-package.sh` fails if a
linker artefact is reintroduced (test it by adding one deliberately).

**Exit criteria:** both plug-ins validated in CI; acceptance tests fail a
deliberately broken render; release artifact is produced by CI, not by hand.

---

### Status - 8 November 2026

Audit fixes landed: piano-roll window lifetime, plugin editor teardown at the
rack choke point, `editorBeingDeleted()` on the editor that was actually
created, project-file fsync around the rename, and `sessions.json` ignored.
The build and `mix_rack` are green.

Still needed for this phase: a test that opens and closes an editor window with
a live and with a gone processor, reclamation of the retired tempo/midi/
automation snapshots, an export path that does not sleep on the message thread,
and a sanitizer or Application Verifier run on Windows.

## 4. Phase 3 — Legal and IP (owner gates, agent implements)

**Decisions needed before any public distribution:**
3.1 **JUCE licence.** Closed-source distribution requires a paid JUCE licence
    (JUCE 8 is AGPLv3 or commercial, and the EULA puts the obligation on the
    product owner). Record the chosen tier in `docs/LICENSING.md`.
3.2 **Fonts.** `din-neuzeit-light.ttf` and `motter-corpus-condensed.ttf` are
    Monotype faces; document-embedding rights do not cover app embedding. Either
    buy app-embedding licences or replace them (Archivo/Inter are already
    bundled and OFL).
3.3 Root `LICENSE`, `THIRD-PARTY-NOTICES.md` (JUCE, VST3 SDK, WebView2, CPM
    deps, fonts), `README.md` (what it is, how to build, how to install).
3.4 A release checklist item: "no new asset ships without a licence line".

**Verification:** files exist; every file in `ui/src/fonts/` has a matching
notice entry or is OFL; a human signs off. No test can prove this.

**Exit criteria:** written decision for JUCE and each font; notices committed.

---

## 5. Phase 4 — Structural refactor

Current sizes: `AudioEngine.cpp` 3,608 · `ArrangeView.cpp` 3,039 ·
`MainComponent.cpp` 2,616 · `Widgets.cpp` 1,125.

**Method** (no behaviour change, one slice per commit):
4.1 Write down the seams first: for `AudioEngine`, the natural units are
    transport/clock, graph build + latency (PDC), render/export, MIDI dispatch,
    device I/O. For `ArrangeView`: clip hit-testing and edits, painting, drag
    state. For `MainComponent`: menu/command wiring, panel hosting, settings.
4.2 Characterization first: for each seam add a test that exercises the public
    API at the boundary (the 160 ctest targets already cover a lot; fill gaps
    before moving code, not after).
4.3 Move code, not behaviour: extract a unit, keep the original function as a
    thin caller, build + `ctest` green, commit. Files move one at a time.
4.4 Budget: no translation unit above ~800 lines when the phase ends.

**Verification**
```bash
ctest --test-dir build -j 4 --output-on-failure   # 160/160 after every slice
wc -l app/Source/*.cpp | sort -rn | head
```
**Exit criteria:** the three files are split along the seams above, suite green
at every commit, no public header churn beyond includes.

**Why not now:** this is a multi-session change with real regression risk and no
interactive app to exercise afterwards; slicing it under a green suite is the
only honest way to do it.

---

## 6. Phase 5 — UI at 150% and accessibility

**Tasks**
6.1 DPI matrix: 100 / 125 / 150 / 200% in Reaper, Cubase and Renoise (Renoise
    is the known double-scaling case for JUCE 8.0.13+), plus Ableton with
    "Auto-Scale Plug-in Window" on and off.
6.2 Verify the pinned JUCE (8.0.17) contains the content-scale fix rather than
    re-introducing a workaround; record the result.
6.3 WebView2: moving the window between monitors with different scaling, and
    host DPI-awareness mismatch (its init failure mode).
6.4 Accessibility pass: visible focus rings, complete tab order, every control
    reachable by keyboard, `prefers-reduced-motion` honoured, contrast checked.
    Add an automated axe-core run to the UI test suite for the static cases.
6.5 Refresh captures in `docs/` and re-score `docs/UI-REVIEW.md`.

**Verification:** a screenshot per host per scale stored in `docs/captures/`,
axe violations 0, keyboard-only pass recorded. **A visual change not looked at
is not verified** — this phase cannot be signed off from a terminal.

**Exit criteria:** no clipping, no double-scaled editors, keyboard-only session
possible end to end.

---

## 7. Phase 6 — Installer and signing

**Tasks**
7.1 Installer: extend the CPack rules already landed (NSIS64 auto-enables when
    `makensis` exists) or add an Inno Setup script — Inno is the friendlier
    format, NSIS is more scriptable; pick one and record why.
7.2 Install targets: `%CommonProgramFiles%\VST3\` for the plug-in bundles,
    per-user `Documents`/`AppData` only for user data. Copy the **whole bundle
    directory**, never the inner DLL.
7.3 Signing: sign the installer **and** both plug-in binaries with `signtool`,
    `/fd SHA256 /tr <RFC3161 url> /td SHA256`. Timestamping is mandatory —
    untimestamped signatures expire with the certificate.
7.4 Certificate: Azure Artifact Signing (~$9.99/mo; orgs USA/CA/EU/UK,
    individuals USA/CA) or an OV certificate. EV no longer bypasses SmartScreen
    (removed 2024), so do not pay for it for that reason. Same identity every
    release so reputation accumulates.
7.5 Clean-VM test in Windows Sandbox: install → scan in two hosts → uninstall →
    assert no leftover files and no orphaned registry entries.

**Verification**
```bash
signtool verify /pa /v ncsound-daw-<version>.exe
signtool verify /pa /v "VST3/808 Designer.vst3/Contents/x86_64-win/808 Designer.vst3"
```
**Exit criteria:** signed installer installs and uninstalls cleanly on a VM,
both plug-ins load in two hosts, `signtool verify` passes on all three binaries.

---

## 8. Phase 7 — Operational readiness (post-1.0, listed for completeness)

Crash reporting (opt-in, with a privacy note), an update check, an 8-hour soak
with the transport running and plug-ins loaded, a performance budget (CPU % per
plug-in at 48 kHz/128 samples), a user guide, and a support/issue process.

---

## 9. Verification loop and evidence

Every phase follows the same loop, and the loop is the deliverable as much as the
code:

1. **Research** — recorded in `docs/superpowers/research/` with sources.
2. **Plan** — this file; each task names its command.
3. **Execute** — one slice per commit, tests written before moves.
4. **Verify** — run the phase's commands, capture the actual output.
5. **Record** — append to `DAW_WORK_LOG.md`: what was run, what passed, what was
   *not* verified and why. No claim without a command.

**Gate rules**
- A check that could not run is reported as a limitation, never as a pass.
- Filtering output through a pipe preserves the exit status (`pipefail` or an
  explicit status capture) — a piped `tail` has already hidden one failure here.
- Native builds need the vcvars wrapper (`build/_nc_plugin_tests.cmd`,
  `build/_nc_pkg_build.cmd`); plain Git Bash has no `windows.h`.

---

## 10. Cannot be verified from this machine

Listed so nobody reads them as done: a clean-VM install, any host load test, the
150/200% DPI matrix, SmartScreen behaviour, an 8-hour soak, code-signing
verification against a real certificate, and anything requiring the JUCE or font
licence paperwork. These need either hardware the agent does not drive or an
owner decision.


## 11. Three phases to 1.0

Sections 2-8 say what has to be true. This regroups what is actually left into
three phases in the order the work can be done: correctness first, then
evidence, then the paperwork that gates distribution. Each phase ends in
something demonstrable, not a status claim.

### Phase A - close the audit (agent, no owner input needed)

Goal: the defects from the 8 November review are fixed or proven gone, not
documented as known issues.

A1. **Reclaim retired snapshots.** `tempoRetired`, `midiRetired` and
    `automationRetired` grow for the life of the session, because listeners that
    are swapped out keep their old snapshot alive and nothing ever drops it.
    Keep at most one retired snapshot per listener, and only until no render
    thread can still hold it - the generation counter already used for the rack
    gives the safe point. Test: 10k tempo edits, assert the containers stay
    bounded.
A2. **Remove the sleep in the export path.** `renderToFile` sleeps on the
    message thread between blocks to let the render thread catch up. Replace it
    with a bounded wait on an event the render thread signals, and assert that
    no `Thread::sleep` remains on that path.
A3. **Lifetime tests.** Open and close an editor window with a live processor,
    then with a processor that is already gone; open and close the piano roll;
    exercise the rack retire path while an editor is open. These are the first
    tests that touch window teardown, so they are also what keeps the 8 November
    fix from regressing.
A4. **Run it under a checker.** Application Verifier basic checks on the test
    executables (MSVC has no usable sanitizer here), then a soak with plug-in
    load/unload cycling.

**Verification:** `mix_rack` plus the new lifetime tests pass; the snapshot test
holds a bound; the soak's memory graph is flat.

**Exit:** no known high- or medium-severity defect left open in the audit.

### Phase B - evidence (CI, plus two runs that need this desktop)

Goal: every gate is enforced by a command someone else can run, and the two
things a terminal cannot prove are recorded.

B1. `pluginval` covers both plug-ins at strictness 5; nightly at 10, with the
    documented false-positive skips named in the workflow.
B2. Acceptance renders: one config per plug-in, rendered output checked in, so a
    DSP regression fails a test instead of shipping.
B3. `tools/verify-package.sh` asserts the package contents (both `.vst3`
    bundles and `moduleinfo.json`, no `.exp`/`.lib`/`.pdb`, no `juceaide`), and a
    tag-triggered workflow builds the artifact and attaches it to the release.
B4. The first real clean-clone CI run - the fetch script has only been run here,
    never on a runner.
B5. 150% DPI and a keyboard-only pass, with captures stored in `docs/captures/`.

**Verification:** three jobs green on a tag; the artifact comes from CI, not
from a local build; captures exist for each host at each scale.

**Exit:** nothing in this phase depends on my shell - the commands are in the
repo and a runner proves them.

### Phase C - distributable (owner decisions gate the start)

Goal: a signed installer that can be published without a legal question
attached.

C1. **Pick one licensing path and record it in `docs/LICENSING.md`.** ChordStudio
    is GPL-3.0 and statically linked, so the combined binary has to be GPL-3.0
    unless a commercial licence is bought for it; JUCE 8 is AGPLv3 or
    commercial. Those two constraints have to be satisfied together, and the
    font question is separate: `din-neuzeit-light.ttf` and
    `motter-corpus-condensed.ttf` are Monotype faces whose embedding rights do
    not cover app embedding, so either buy that or ship Inter/Archivo, which are
    already bundled and OFL.
C2. Root `LICENSE`, `THIRD-PARTY-NOTICES.md`, `README.md`.
C3. Version to 1.0.0, tag `v1.0.0`, installer targeting
    `%CommonProgramFiles%\VST3`, signing with an RFC3161 timestamp on the
    installer and both plug-in binaries.
C4. Windows Sandbox: install, scan in two hosts, uninstall, assert no leftovers.

**Verification:** `signtool verify /pa` passes on all three binaries; the sandbox
round trip leaves nothing behind.

**Exit:** a tagged, signed, installable build whose licence story is written
down.

### Ordering

A blocks nothing but its own exit criteria and should come first, because the
snapshot fix touches the same retire path the new tests cover. B can start once
A1-A2 are in. C1 can be decided in parallel at any time, but C2-C4 should not
start until it is decided - the answer changes the notices, the README and the
installer text.


### Status - 8 November 2026 (Phase A)

A1 is done: replaced snapshots are stamped with the audio-block counter and
freed once a later callback has started, with a test that drives 10k tempo edits
and 2k MIDI replacements and asserts the lists drain to zero.

Remaining in Phase A: A2 (remove the export-path sleep), A3 (lifetime tests for
editor and piano-roll windows), A4 (Application Verifier plus a load/unload
soak).


### Status - 8 November 2026 (Phase A, continued)

A2 is done: the export hand-off waits for callback signals instead of sleeping
for two block durations.

Remaining in Phase A: A3 (lifetime tests for the editor and piano-roll windows),
A4 (Application Verifier plus a load/unload soak).


### Status - 8 November 2026 (Phase A, A3)

A3 partly done: the plugin editor window has lifetime coverage (4 cases, 19
assertions) through `pluginEditors::closeWindowsFor()`, the path the rack uses,
with a stub processor so no plug-in is needed. The piano roll is not covered -
its window class is private to `ArrangeView.cpp` and needs extracting first.
