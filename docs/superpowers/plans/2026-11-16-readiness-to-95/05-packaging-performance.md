# Packaging, Soak and Release Evidence Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development
> (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use
> checkbox (`- [ ]`) syntax for tracking.

**Goal:** Take distribution from 35/100 and stability from 75/100 to 95 each: a soak that proves the
audio thread survives 30 minutes with hosted plugins, a package that contains the app, both suite
VST3s and the legal notices, and a written record of the gates that only a human with hardware can
close.

**Architecture:** `cmake/Packaging.cmake` already configures CPack (`CPACK_PACKAGE_NAME "NCSound DAW"`,
components `plugins;docs`) and already degrades gracefully when `NC_MAKENSIS_EXECUTABLE` is
`NOTFOUND`. The ZIP path works anywhere; the NSIS installer stays an owner gate. This plan adds an
`app` component, a notices check, a repeatable soak harness, and `docs/RELEASE-GATES.md`.

**Tech Stack:** CMake 3.x + CPack (ZIP), Ninja, Node scripts under `scripts/`, JUCE `File`/`Process`.

**Spec:** `docs/superpowers/plans/2026-11-16-readiness-to-95/README.md` and `.planning/ROADMAP.md`
Phases 6 and 5 (the human gates).

## Global Constraints

- Packaging must not require NSIS or WiX to succeed; both stay optional and gated on the executable
  being found.
- The soak harness must be runnable today and must fail loudly (non-zero exit) when it drops a block.
- Nothing under `third_party/` may be redistributed without a notice entry; the check is a script,
  not a checklist a human is trusted to remember.
- Digest and 195/195 must hold after every task.

## Review Focus

- **Plugin that crashes during the soak** — the harness must record which plugin and keep going;
  a crash must not be reported as a pass.
- **Soak on a machine with no VST3 folder configured** — must report "no hosted plugins exercised"
  rather than claiming a 30-minute hosted run.
- **Package built on a clean machine** — must not be missing the app binary because a component was
  only added to the install tree on one generator.
- **A notice file present but containing the wrong version string** — the check compares against
  `third_party/JUCE_PIN.txt`, not just for existence.
- **ZIP extracted over an existing install** — must not accumulate duplicate `LICENSE` files with
  different contents.

---

## Task 1: A repeatable soak harness

**Files:**
- Create: `scripts/soak.mjs`
- Create: `app/Source/SoakTests.cpp` (one `[.][soak]` case — hidden from the default run)
- Modify: `app/CMakeLists.txt` (register the file)

**Interfaces:**
- Produces: `node scripts/soak.mjs --minutes 30 --out build/soak-report.txt`, exiting `0` only if
  every recorded block rendered and no overrun was observed.
- The native case carries the Catch2 tag `[.][soak]` so `ctest` never runs it by default.

- [ ] **Step 1: Write the failing test (the native case)**

```cpp
TEST_CASE ("soak renders hosted inserts for the requested duration", "[.][soak]")
{
    nc::AudioEngine engine;
    engine.setActiveTrackCount (8);
    engine.createDemoAssets();
    engine.prepareForRender (48000.0, 512);

    const auto minutes = juce::SystemStats::getEnvironmentVariable ("NC_SOAK_MINUTES", "1").getIntValue();
    const auto blocks = (int) (minutes * 60.0 * 48000.0 / 512.0);

    for (int i = 0; i < blocks; ++i)
        engine.processBlockForTesting();

    CHECK (engine.recordOverflows() == 0);
}
```

- [ ] **Step 2: Run it and watch it fail**

Run: `build/app/NCMixRackTests_artefacts/Release/NCMixRackTests.exe "[soak]" -s`
Expected: FAIL — no matching test (tag not yet added).

- [ ] **Step 3: Register the file and implement the harness**

`scripts/soak.mjs` spawns the test binary with `NC_SOAK_MINUTES` set, tees output to `--out`, and
exits non-zero on a non-zero child exit. It prints the hosted-plugin count from the state and writes
`no hosted plugins exercised` into the report when it is zero.

- [ ] **Step 4: Run it for one minute, then the gate**

Run: `node scripts/soak.mjs --minutes 1 --out build/soak-smoke.txt` → exit 0, report non-empty.
Then native build exit 0 and `ctest` 195/195 unchanged (the soak case is hidden).

- [ ] **Step 5: Commit** — `test(soak): a repeatable 30-minute audio-thread harness`.

---

## Task 2: Record the 30-minute result

**Files:**
- Modify: `DAW_WORK_LOG.md`

- [ ] **Step 1: Run the real soak** — `node scripts/soak.mjs --minutes 30 --out build/soak-report.txt`
  (run it in the background; the command cap is ~295 s and this takes 30 minutes).
- [ ] **Step 2: Paste the summary line** — duration, blocks rendered, `recordOverflows`, hosted
  plugin count — into `DAW_WORK_LOG.md` under a dated heading, with the machine noted.
- [ ] **Step 3: If overflows are non-zero, do not record a pass.** File the count and move on with the
  plan's other tasks; performance stays below 95 and that is the honest score.
- [ ] **Step 4: Commit** — `docs: record the 30-minute soak result`.

---

## Task 3: Package the app with the plugins and notices

**Files:**
- Modify: `cmake/Packaging.cmake`
- Modify: `app/CMakeLists.txt` (install rule for the GUI binary)
- Test: the packaged artifact listing

**Interfaces:**
- Produces: a `CPACK_COMPONENTS_ALL` of `app;plugins;docs` and an install target
  `install(TARGETS NCSoundHub RUNTIME DESTINATION . COMPONENT app)`.

- [ ] **Step 1: Write the failing check**

```bash
cpack --config build/CPackConfig.cmake -G ZIP -B build/pkg
unzip -l build/pkg/ncsound-daw-*.zip | grep -E "NCSound Music Hub.exe|\.vst3|LICENSE|THIRD-PARTY-NOTICES"
```

Expected before the change: the app binary is absent from the listing.

- [ ] **Step 2: Add the component and the install rule.**
- [ ] **Step 3: Re-run the check** — all four patterns found.
- [ ] **Step 4: Gate** — native builds still exit 0; `ctest` 195/195.
- [ ] **Step 5: Commit** — `build(pkg): ship the app, plugins and notices in one archive`.

---

## Task 4: Notices check against the JUCE pin

**Files:**
- Create: `scripts/check-notices.mjs`
- Test: the script itself

**Interfaces:**
- Produces: `node scripts/check-notices.mjs`, exit 0 when every entry in `third_party/` and every
  vendored plugin has a line in `THIRD-PARTY-NOTICES.md`, and the JUCE version quoted there equals
  the `tag:` line in `third_party/JUCE_PIN.txt`.

- [ ] **Step 1: Write the failing check** — run it now; it should fail if any vendored tree lacks a
  notice or the JUCE version string has drifted.
- [ ] **Step 2: Implement the script** — parse `JUCE_PIN.txt`, scan `third_party/*` directories and
  `plugins/*`, and require each name to appear in `THIRD-PARTY-NOTICES.md`.
- [ ] **Step 3: Fix whatever it reports** by adding the missing notice lines.
- [ ] **Step 4: Re-run to exit 0.**
- [ ] **Step 5: Commit** — `build(legal): check notices against the pinned dependencies`.

---

## Task 5: Write the human gates down

**Files:**
- Create: `docs/RELEASE-GATES.md`

**Interfaces:**
- Produces: one section per gate that cannot be automated from this machine, each with the exact
  steps, what to look for, and where to record the result.

- [ ] **Step 1: Write the file** with these five gates:
  1. **Recording with real hardware** — arm a track, record 3 s, confirm the take lands with peaks
     (closes the recording department's remaining 10 points).
  2. **Session round trip with the real chooser** — save, quit, reopen, confirm identical state
     (complements plan 03's document-level test).
  3. **Installer run** — build NSIS on a machine with `makensis`, install, launch, uninstall.
  4. **Hosted-plugin soak on the target machine** — the 30-minute run from Task 2, repeated there.
  5. **Listening pass** — a 5-minute play through the demo session at 0.45 load, noting any crackle.
- [ ] **Step 2: Link each gate from `.planning/ROADMAP.md`** so the "OPEN" markers point at it.
- [ ] **Step 3: Commit** — `docs: write the release gates that need a human and hardware`.

---

## Self-Review

- Roadmap Phase 6 asks for the soak, pluginval strictness and an installer: the soak is Tasks 1-2,
  the installer is Task 3 plus gate 3, and pluginval stays in CI where it already runs at
  strictness 5.
- Review Focus: crashing plugin is Task 1's report format (name + keep going); zero hosted plugins is
  the harness's explicit message; missing app binary is Task 3's listing check; version drift is Task
  4's pin comparison; duplicate notices on re-extract are avoided by installing one top-level
  `LICENSE`, not one per component.
