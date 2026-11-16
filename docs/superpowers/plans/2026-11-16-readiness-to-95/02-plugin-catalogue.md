# Plugin Catalogue Reliability Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development
> (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use
> checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the VST3 catalogue trustworthy on a cold start and cheap on every later start: honest
progress, a timeout that hits one bundle instead of the whole scan, a resumable index so a relaunch
does not re-scan 25 bundles, and no row that stays "Missing" after a completed scan.

**Architecture:** `PluginCatalog` (a `juce::Thread`) already owns the scan loop, exposes
`scanProgress()`, `isScanning()`, `lastScanStatus()`, `mergeScannedDescriptions(...)` and injects a
`nc::ScannerBackend` whose `scan(formatName, fileOrIdentifier)` returns a `ScannerReply` with
`timedOut` / `error` / `connectionLost`. `PluginScanner` already kills a timed-out worker. What is
missing is (a) progress that advances per bundle, (b) persistence of successful scans keyed by bundle
size and mtime, and (c) status rules that cannot leave a row Missing forever.

**Tech Stack:** C++20/JUCE, Catch2 native tests with a fake `ScannerBackend`, React/Vitest for the
progress surface.

**Spec:** `docs/superpowers/plans/2026-11-16-readiness-to-95/README.md` and `.planning/ROADMAP.md`
Phase 3.

## Global Constraints

- Never scan on the message thread; `scanAsync` must return before any bundle is touched.
- One unreadable or crashing bundle must never abort the scan of the others.
- The index file lives at `juce::File::getSpecialLocation (userApplicationDataDirectory)
  .getChildFile ("NCSound Music Hub").getChildFile ("plugin-index.json")`.
- Digest and 195/195 must hold after every task.

## Review Focus

- **Two bundles with the same product name** — must stay two rows keyed by path, never collapse and
  never load the wrong one.
- **A bundle whose mtime changes but size does not** — must be re-scanned, or an upgraded plugin
  silently keeps its old parameters.
- **A cache written by an older app version** — must be ignored wholesale rather than half-read.
- **Cancelling mid-scan** — must leave the index as it was, never a truncated one.
- **A scan that finds zero plugins** — must say "no VST3 folders configured", not sit at 0% forever.

---

## Task 1: Progress that advances per bundle

**Files:**
- Modify: `app/Source/PluginCatalog.cpp` (`run()` loop)
- Test: `app/Source/PluginCatalogTests.cpp`

**Interfaces:**
- Consumes: `ScannerBackend::scan (const juce::String& formatName, const juce::String& fileOrIdentifier)`,
  `PluginCatalog::scanProgress() const`.
- Produces: no signature change; `scanProgress()` becomes monotonic and reaches exactly `1.0f` only
  when the scan finishes.

- [ ] **Step 1: Write the failing test**

```cpp
TEST_CASE ("scan progress advances once per bundle", "[catalog]")
{
    auto backend = std::make_unique<FakeScannerBackend>();
    backend->bundles = { "a.vst3", "b.vst3", "c.vst3" };
    backend->onScan = [](int index) { juce::Thread::sleep (40); return ScannerReply{}; };

    nc::PluginCatalog catalog ({}, std::move (backend));
    std::vector<float> seen;
    catalog.scanAsync ([&] (bool, juce::String) {});

    while (catalog.isScanning())
    {
        seen.push_back (catalog.scanProgress());
        juce::Thread::sleep (10);
    }

    REQUIRE (seen.size() > 3);
    CHECK (seen.front() < 0.5f);
    CHECK (std::is_sorted (seen.begin(), seen.end()));
    CHECK (seen.back() == 1.0f);
}
```

`FakeScannerBackend` is the existing fake in `PluginCatalogTests.cpp`; extend it with a `bundles`
list and an `onScan` hook rather than writing a second one.

- [ ] **Step 2: Run it and watch it fail**

Run: `build/app/NCMixRackTests_artefacts/Release/NCMixRackTests.exe "[catalog]" -s`
Expected: FAIL — progress only moves at the end, so `seen` contains one 0.0 and one 1.0.

- [ ] **Step 3: Set progress after each bundle**

In the scan loop, after each `backend->scan(...)` reply, store
`progress = (float) (index + 1) / (float) std::max (1, bundles.size())` with
`std::memory_order_relaxed`. A timed-out or failed bundle still advances the counter — it consumed
its slot — but appends its status to `lastScanStatus()` instead of a description.

- [ ] **Step 4: Run it and watch it pass**, then the gate: native build exit 0, `ctest` 195/195.

- [ ] **Step 5: Commit** — `fix(catalog): advance scan progress per bundle`.

---

## Task 2: Persistent index, keyed by size and mtime

**Files:**
- Create: `app/Source/PluginIndexCache.h`, `app/Source/PluginIndexCache.cpp`
- Test: `app/Source/PluginCatalogTests.cpp` (new `[index]` cases)

**Interfaces:**
- Produces:

```cpp
namespace nc::pluginindex
{
struct Entry
{
    juce::String path;
    juce::int64 size = 0;
    juce::int64 modifiedMs = 0;
    juce::String stateXml;   // PluginDescription::createXml()->toString()
};

/** Returns entries whose bundle still exists with the same size and mtime. */
std::vector<Entry> loadValid (const juce::File& indexFile);

bool save (const juce::File& indexFile, const std::vector<juce::PluginDescription>& descriptions);
}
```

- [ ] **Step 1: Write the failing test**

```cpp
TEST_CASE ("the index round-trips and drops a bundle whose mtime changed", "[index]")
{
    const auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                         .getChildFile ("nc-index-test");
    dir.deleteRecursively(); dir.createDirectory();
    const auto bundle = dir.getChildFile ("x.vst3");
    bundle.create(); bundle.appendText ("one");
    const auto index = dir.getChildFile ("plugin-index.json");

    juce::PluginDescription d;
    d.name = "X"; d.fileOrIdentifier = bundle.getFullPathName();
    REQUIRE (nc::pluginindex::save (index, { d }));
    CHECK (nc::pluginindex::loadValid (index).size() == 1);

    bundle.appendText ("two");   // size changes
    CHECK (nc::pluginindex::loadValid (index).empty());
}
```

- [ ] **Step 2: Run it and watch it fail** — the header does not exist.
- [ ] **Step 3: Implement the cache**

`save` collects `{ path, (int64) file.getSize(), (int64) file.getLastModificationTime().toMilliseconds(), xml }`
per description and writes them with `juce::JSON::toString`. `loadValid` parses, and keeps only
entries whose file exists with the recorded size **and** mtime; a parse error returns an empty vector
(the whole cache is ignored, never half-read).

- [ ] **Step 4: Run the test, then the gate.**
- [ ] **Step 5: Commit** — `feat(catalog): persist a size-and-mtime keyed plugin index`.

---

## Task 3: Cold start uses the index

**Files:**
- Modify: `app/Source/PluginCatalog.h/.cpp` (constructor takes the index file)
- Modify: `app/Source/MainComponent.cpp` (pass the real path)
- Test: `app/Source/PluginCatalogTests.cpp`

**Interfaces:**
- Produces:

```cpp
explicit PluginCatalog (std::vector<PluginInfo> registryEntries = {},
                        std::unique_ptr<ScannerBackend> scannerToUse = {},
                        juce::File indexFile = {});
```

- [ ] **Step 1: Write the failing test**

```cpp
TEST_CASE ("a warm index skips the worker entirely", "[catalog]")
{
    // ... build a fake backend, scan once with an index file in a temp dir ...
    auto second = std::make_unique<FakeScannerBackend>();
    auto* raw = second.get();
    nc::PluginCatalog catalog ({}, std::move (second), indexFile);
    catalog.scanSync();
    CHECK (raw->scanCount == 0);
    CHECK (catalog.snapshot().size() == 1);
}
```

- [ ] **Step 2: Run it and watch it fail** — the second catalog re-scans.
- [ ] **Step 3: Load the index in `scanSync`/`scanAsync` before touching the backend.** Merge via the
  existing `mergeScannedDescriptions`, set `progress = 1.0f`, set status `"cached (n plugins)"`, and
  return. Write the index at the end of a real scan.
- [ ] **Step 4: Run the tests, then the gate.**
- [ ] **Step 5: Commit** — `feat(catalog): reuse the index on a warm start`.

---

## Task 4: No permanently-Missing rows

**Files:**
- Modify: `app/Source/PluginCatalog.cpp` (row status rules)
- Test: `app/Source/PluginCatalogTests.cpp`

**Interfaces:**
- Consumes: `CatalogEntry::status` (`PluginStatus::external` / `missing`), `CatalogEntry::scanned`.

- [ ] **Step 1: Write the failing test**

```cpp
TEST_CASE ("a registry row is Missing only after a completed scan", "[catalog]")
{
    // registry names A.vst3; no bundle on disk, fake backend returns an empty reply
    CHECK (beforeScan.status == nc::PluginStatus::external);
    catalog.scanSync();
    const auto after = catalog.find ("a");
    REQUIRE (after.has_value());
    CHECK (after->status == nc::PluginStatus::missing);
}
```

- [ ] **Step 2: Run it and watch it fail** if rows are marked Missing while `isScanning()`.
- [ ] **Step 3: Apply the rule:** a registry row is `missing` only when `! isScanning()` and no scanned
  row matches its `vst3BundleName`; while scanning it keeps `external`. A scanned row always wins over
  the registry row for the same bundle.
- [ ] **Step 4: Run the test, then the gate.**
- [ ] **Step 5: Commit** — `fix(catalog): never report Missing during a scan`.

---

## Task 5: Show scan progress in the browser

**Files:**
- Modify: `ui/src/workspace/Browser.tsx`
- Test: `ui/src/workspace/Browser.test.tsx`

**Interfaces:**
- Consumes: `state.scan = { scanning: boolean, progress: number, status: string }` — add those three
  fields to the state pushed by `buildState()` in `WebViewBridge.cpp` from
  `engine.pluginCatalog().isScanning() / scanProgress() / lastScanStatus()`.

- [ ] **Step 1: Write the failing test**

```tsx
it("shows bundle progress while scanning", () => {
  render(<Browser state={{ ...emptyState, scan: { scanning: true, progress: 0.48, status: "12/25" } }} />);
  expect(screen.getByRole("progressbar")).toHaveAttribute("aria-valuenow", "48");
  expect(screen.getByText("12/25")).toBeInTheDocument();
});
```

- [ ] **Step 2: Run it and watch it fail.**
- [ ] **Step 3: Render a `role="progressbar"` with `aria-valuenow={Math.round(progress*100)}` and the
  status text next to it; hide it when `scanning` is false. Add the three fields to `buildState()`.
- [ ] **Step 4: Gate** — vitest PASS, native build exit 0, `ctest` 195/195, `gen:routes` not needed
  (state shape only).
- [ ] **Step 5: Commit** — `feat(browser): show real scan progress`.

---

## Self-Review

- Roadmap Phase 3 exit criteria map to: cold start in one pass (Tasks 2-3), no permanently-Missing row
  (Task 4), second launch reuses the cache (Task 3), progress reflects work (Tasks 1 and 5).
- Review Focus: same-name bundles are keyed by `path` in `pluginindex::Entry`; mtime-only change is
  Task 2's second assertion; old cache is all-or-nothing in `loadValid`; cancel leaves the index
  untouched because it is only written after a completed scan; zero-plugin message is Task 4's status
  rule plus Task 5's text.
