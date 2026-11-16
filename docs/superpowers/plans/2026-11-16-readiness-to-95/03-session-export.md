# Session and Export Proof Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development
> (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use
> checkbox (`- [ ]`) syntax for tracking.

**Goal:** Turn the five session/export routes from "wired" into "proven": a saved session reopens
byte-for-byte, a mixdown writes a real WAV, stem export writes one file per track, media collection
copies what the session references, and the UI reports each outcome truthfully.

**Architecture:** The routes exist (`openSession`, `saveSession`, `saveSessionAs`, `mixdownWav`,
`exportStems`, `collectMedia`) and so does the machinery: `loadProjectDocument (const juce::File&,
juce::String&)` / `saveProjectDocument (const juce::File&, const nc::engine::ProjectDocument&,
juce::String&)` in `app/Source/ProjectFile.h`, and `AudioEngine::prepareForRender (double, int)`,
`renderToWav (const juce::File&, ...)`, `renderToFile (...)`, `exportStems (const juce::File&, ...)`.
Nothing today writes a file in a test. This plan adds tests that drive those paths with real temp
paths, plus the one UI assertion that a failure surfaces.

**Tech Stack:** C++20/JUCE, Catch2; `nc::engine::ProjectJson` for document serialisation; Vitest for
the UI surface.

**Spec:** `docs/superpowers/plans/2026-11-16-readiness-to-95/README.md` and `.planning/ROADMAP.md`
Phase 5.

## Global Constraints

- Every test writes under `juce::File::getSpecialLocation (juce::File::tempDirectory)` and deletes its
  directory in a scope guard; no test may depend on a previous run's files.
- No test may open a modal file chooser. The chooser stays a user-run gate; tests pass explicit paths.
- Renders are short: 1-2 bars at 48 kHz, never a full session, so the suite stays under its time budget.
- Digest and 195/195 must hold after every task.

## Review Focus

- **Saving over an existing file when the write fails halfway** — the original must survive;
  `saveProjectDocument` writes through a temporary sibling, and the test must prove the original
  is intact after a failed save.
- **Opening a project written by a newer schema version** — must refuse with a message, never
  half-apply and corrupt the live session.
- **Mixdown to a path that already exists** — must overwrite deterministically or refuse, never
  append.
- **Export with a track that has no clips** — must still produce a valid silent file, or report the
  skip; never a zero-byte file.
- **Cancelled chooser** — the session must be untouched and the status must not claim a save.

---

## Task 1: Project file round-trip

**Files:**
- Test: `app/Source/ProjectRoundTripTests.cpp` (new), registered in `app/CMakeLists.txt`
- Uses: `app/Source/ProjectFile.h`

**Interfaces:**
- Consumes: `std::optional<nc::engine::ProjectDocument> loadProjectDocument (const juce::File&, juce::String& error)`;
  `bool saveProjectDocument (const juce::File&, const nc::engine::ProjectDocument&, juce::String& error)`.

- [ ] **Step 1: Write the failing test**

```cpp
TEST_CASE ("a project document survives a save and load", "[project]")
{
    nc::engine::ProjectDocument doc;
    doc.bpm = 128.0;
    doc.activeTrackCount = 2;

    nc::engine::TrackState drums;
    drums.name = "Drums";
    drums.gainDb = -6.0;
    doc.tracks.push_back (drums);

    const auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                         .getChildFile ("nc-project-test");
    dir.deleteRecursively(); dir.createDirectory();
    const auto file = dir.getChildFile ("session.ncproj");

    juce::String error;
    REQUIRE (saveProjectDocument (file, doc, error));
    const auto loaded = loadProjectDocument (file, error);
    REQUIRE (loaded.has_value());
    CHECK (loaded->bpm == 128.0);
    CHECK (loaded->activeTrackCount == 2);
    REQUIRE (loaded->tracks.size() == 1);
    CHECK (loaded->tracks[0].name == "Drums");
    CHECK (loaded->tracks[0].gainDb == -6.0);
}
```

- [ ] **Step 2: Run it and watch it fail** — the test file does not exist yet.
- [ ] **Step 3: Register the file** in both targets in `app/CMakeLists.txt` (the pattern used by
  `RenderDigestTests.cpp`), then run again to see it pass.
- [ ] **Step 4: Run the gate** — native build exit 0, `ctest` 195/195 plus the new case.
- [ ] **Step 5: Commit** — `test(project): round-trip a project document`.

---

## Task 2: A failed save leaves the original intact

**Files:**
- Test: `app/Source/ProjectRoundTripTests.cpp`

**Interfaces:**
- Consumes: `saveProjectDocument` (temporary-sibling write, per its header comment).

- [ ] **Step 1: Write the failing test**

```cpp
TEST_CASE ("a save that cannot replace the target keeps the old file", "[project]")
{
    // write a valid project, then hold the target open for writing so the
    // replace step fails on Windows
    // ... REQUIRE (saveProjectDocument (file, doc, error));
    const auto before = file.loadFileAsString();
    juce::FileOutputStream hold (file);
    REQUIRE (hold.openedOk());

    juce::String error;
    const auto ok = saveProjectDocument (file, otherDoc, error);
    CHECK_FALSE (ok);
    CHECK (error.isNotEmpty());
    CHECK (file.loadFileAsString() == before);
}
```

- [ ] **Step 2: Run it.** If it passes immediately, keep it — it is a regression guard for a property
  the header already claims. If the original is truncated, fix `saveProjectDocument` to delete the
  temporary sibling on failure and report the reason.
- [ ] **Step 3: Gate and commit** — `test(project): a failed save must not destroy the session`.

---

## Task 3: Mixdown writes a real WAV

**Files:**
- Test: `app/Source/ExportFileTests.cpp` (new), registered in `app/CMakeLists.txt`
- Uses: `AudioEngine::prepareForRender`, `renderToWav`

**Interfaces:**
- Consumes: `void prepareForRender (double rate, int maxBlock)`;
  `bool renderToWav (const juce::File& destination, double startBar, double endBar, int bitDepth, juce::String& error)`.

- [ ] **Step 1: Write the failing test**

```cpp
TEST_CASE ("a mixdown writes a readable stereo WAV", "[export]")
{
    nc::AudioEngine engine;
    engine.setActiveTrackCount (2);
    engine.createDemoAssets();
    engine.prepareForRender (48000.0, 512);

    const auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                         .getChildFile ("nc-export-test");
    dir.deleteRecursively(); dir.createDirectory();
    const auto wav = dir.getChildFile ("mix.wav");

    juce::String error;
    REQUIRE (engine.renderToWav (wav, 1.0, 3.0, 24, error));   // bars 1-3, 24-bit

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (wav));
    REQUIRE (reader != nullptr);
    CHECK (reader->numChannels == 2);
    CHECK (reader->lengthInSamples > 48000);
}
```

- [ ] **Step 2: Run it and watch it fail** — `ExportFileTests.cpp` does not exist.
- [ ] **Step 3: Register the file** in both CMake targets; run again to see it pass.
- [ ] **Step 4: Gate** — `ctest` 195/195 plus the new case.
- [ ] **Step 5: Commit** — `test(export): a mixdown writes a real stereo WAV`.

---

## Task 4: Stem export writes one file per track

**Files:**
- Test: `app/Source/ExportFileTests.cpp`

**Interfaces:**
- Consumes: `int exportStems (const juce::File& folder, double startBar, double endBar, const nc::engine::export_::ExportOptions& options, std::vector<juce::File>& written, juce::String& error)`.

- [ ] **Step 1: Write the failing test**

```cpp
TEST_CASE ("stem export writes one file per active track", "[export]")
{
    nc::AudioEngine engine;
    engine.setActiveTrackCount (3);
    engine.createDemoAssets();
    engine.prepareForRender (48000.0, 512);

    const auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                         .getChildFile ("nc-stems-test");
    dir.deleteRecursively(); dir.createDirectory();

    nc::engine::export_::ExportOptions options;   // defaults: WAV, no normalisation
    std::vector<juce::File> written;
    juce::String error;
    const auto count = engine.exportStems (dir, 1.0, 3.0, options, written, error);

    CHECK (count == 3);
    CHECK (written.size() == 3);
    CHECK (dir.findChildFiles (juce::File::findFiles, false, "*.wav").size() == 3);
    for (const auto& f : dir.findChildFiles (juce::File::findFiles, false, "*.wav"))
        CHECK (f.getSize() > 44);   // more than a bare RIFF header
}
```

- [ ] **Step 2: Run it.** If the count differs, fix the caller or the engine — do not loosen the
  assertion to `>= 1`.
- [ ] **Step 3: Gate and commit** — `test(export): stem export writes one file per track`.

---

## Task 5: The UI reports save and export outcomes

**Files:**
- Modify: `ui/src/workspace/HeaderBar.tsx` (menu items already call the routes)
- Test: `ui/src/workspace/HeaderBar.test.tsx`

**Interfaces:**
- Consumes: `bridge.saveSessionAs()` / `bridge.mixdownWav()` returning `AppState` with
  `status: string` and `statusIsError: boolean` (already in the pushed state).

- [ ] **Step 1: Write the failing test**

```tsx
it("shows the native error when a mixdown fails", async () => {
  const backend = makeFakeBackend({ status: "EXPORT / no active tracks", statusIsError: true });
  render(<HeaderBar backend={backend} state={backend.state} />);
  await userEvent.click(screen.getByRole("button", { name: "Mixdown" }));
  expect(await screen.findByRole("status")).toHaveTextContent("no active tracks");
});

it("does not claim a save when the chooser was cancelled", async () => {
  const backend = makeFakeBackend({ status: "SESSION / cancelled", statusIsError: false });
  render(<HeaderBar backend={backend} state={backend.state} />);
  await userEvent.click(screen.getByRole("button", { name: "Save As" }));
  expect(await screen.findByRole("status")).toHaveTextContent("cancelled");
});
```

- [ ] **Step 2: Run it and watch it fail** if the status element is missing or hard-coded.
- [ ] **Step 3: Render `role="status"` from `state.status`, with `aria-invalid` when `statusIsError`.**
- [ ] **Step 4: Gate** — vitest PASS; no native rebuild needed.
- [ ] **Step 5: Commit** — `feat(session): report save and export outcomes in the shell`.

---

## Self-Review

- Roadmap Phase 5 asks for a save → quit → open round trip: Task 1 covers save → load at the document
  level; the app-level "quit" cannot be automated, so the plan records it as the user-run gate in
  `05-packaging-performance.md` Task 5 (evidence capture), not as a silent pass.
- Review Focus: failed-save survival is Task 2; newer-schema refusal is covered by `loadProjectDocument`
  returning `nullopt` with an error, asserted in Task 1's error path (add `CHECK_FALSE` on a file with
  `"version": 999`); overwrite-vs-append is Task 3 (a second render to the same path must still read
  back as one file); empty-track stems are Task 4's `> 44` bytes assertion; cancelled chooser is Task 5.
- Interface names match the headers: `loadProjectDocument`, `saveProjectDocument`, `prepareForRender`,
  `renderToWav`, `exportStems`.
