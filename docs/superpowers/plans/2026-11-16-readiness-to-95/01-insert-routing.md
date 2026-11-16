# Insert and Routing Completion Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development
> (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use
> checkbox (`- [ ]`) syntax for tracking.

**Goal:** Close the five gaps that hold mixing and routing at 60/100 — insert bypass, drag-reorder,
plugin editor access, master A/B, and an automation read toggle — all reachable from the React
shell.

**Architecture:** The engine already implements every operation; none of it is reachable. `AudioEngine`
exposes `setTrackInsertBypassed(int,int,bool)`, `moveTrackInsert(int,int,int)`,
`masterPluginStateBase64()`, `restoreMasterPlugin(id, stateBase64)`,
`trackInsertInstance(int,int)`, `trackInstrumentInstance(int)` and `masterInsertInstance()`, and
`ProjectState::PluginSlotState` already carries `bool bypassed`. What is missing is the bridge route,
the typed `bridge.ts` method, and the control in the strip. This plan adds no DSP and changes no
engine signature except one automation gate (Task 5).

**Tech Stack:** C++20/JUCE route handlers in `app/Source/WebViewBridge.cpp`; React 19 + TypeScript in
`ui/src`; Vitest + Testing Library for UI tests; Catch2 for native tests.

**Spec:** `docs/superpowers/plans/2026-11-16-readiness-to-95/README.md`, plus `.planning/ROADMAP.md`
Phase 2.

## Global Constraints

- Native builds only via `build/_nc_mixrack_build.cmd` and `build/_ncbuild_app.cmd`.
- Run `cd ui && npm run gen:routes` after every route change; never edit `nativeRoutes.generated.ts`.
- Engine calls are made on the message thread inside the route handler; the handler answers with
  `done (buildState())` like `setChannelParam` does.
- Slot numbering: instrument lane = `-1`, master insert = `-2`. Effects use `0..7`.
- Digest `0x1b5398dd4a9f297a` and 195/195 must hold after every task.

## Review Focus

- **Bypass while an editor window is open** — audio must go dry and the window must survive; a
  bypass that closes or crashes the editor is the bug this feature creates.
- **Reorder onto a slot that holds a different plugin** — must refuse and report, never overwrite
  silently; the user's plugin state is irreplaceable.
- **A/B recall when the stored master plugin is missing from disk** — must report and leave the
  current rack untouched, never restore an empty rack.
- **AUTO off with lanes present** — the fader must return to its static value and stay there; a
  lane that keeps driving gain after the toggle is off is worse than no toggle.
- **Editor route for an empty slot** — must answer `{ ok: false, error }` and create no window.

---

## Task 1: Insert bypass

**Files:**
- Modify: `app/Source/WebViewBridge.cpp` (route table, near `loadPlugin`)
- Modify: `ui/src/lib/bridge.ts`
- Modify: `ui/src/workspace/Mixer.tsx` (strip slot list)
- Test: `ui/src/workspace/Mixer.test.tsx`

**Interfaces:**
- Consumes: `AudioEngine::setTrackInsertBypassed (int track, int slot, bool bypassed)`.
- Produces: route `setInsertBypassed(track, slot, bypassed) => AppState`; `bridge.setInsertBypassed(track: number, slot: number, bypassed: boolean): Promise<AppState>`.

- [ ] **Step 1: Write the failing test**

```tsx
it("alt-clicks a loaded slot to bypass it", async () => {
  const backend = makeFakeBackend({ slots: [{ slot: 0, name: "Pro-Q", bypassed: false }] });
  render(<Mixer backend={backend} state={stateWithInsert(0, 0)} />);
  await userEvent.click(screen.getByLabelText("Insert 1 Pro-Q"), { altKey: true });
  expect(backend.setInsertBypassed).toHaveBeenCalledWith(0, 0, true);
});
```

- [ ] **Step 2: Run it and watch it fail**

Run: `cd ui && npx vitest run src/workspace/Mixer.test.tsx`
Expected: FAIL — `backend.setInsertBypassed is not a function`.

- [ ] **Step 3: Add the route and the control**

Register in the route table:

```cpp
{ "setInsertBypassed", [this] (const Args& a, Completion done)
  {
      const auto track = a.size() > 0 ? (int) a[0] : -1;
      const auto slot  = a.size() > 1 ? (int) a[1] : -1;
      const auto off   = a.size() > 2 ? (bool) a[2] : false;

      if (track >= 0 && track < engine.getActiveTrackCount() && slot >= 0 && slot < 8)
      {
          engine.setTrackInsertBypassed (track, slot, off);
          setStatus (off ? "INSERT / bypassed" : "INSERT / active");
      }
      done (buildState());
  } },
```

Add to `bridge.ts`:

```ts
/** Bypasses one insert slot without unloading it. */
setInsertBypassed(track: number, slot: number, bypassed: boolean): Promise<AppState> {
  return call<AppState>("setInsertBypassed", track, slot, bypassed);
}
```

In `Mixer.tsx`, give each slot button `aria-label={`Insert ${slot + 1} ${name}`}` and
`onClick={(e) => e.altKey && backend.setInsertBypassed(track, slot, !bypassed)}`, rendering a
struck-through style from `slot.bypassed`.

- [ ] **Step 4: Run the test and the gate**

Run: `cd ui && npx vitest run src/workspace/Mixer.test.tsx` → PASS
Run: `cmd.exe //c call build/_nc_mixrack_build.cmd` → exit 0; `ctest --test-dir build -C Release` → 195/195

- [ ] **Step 5: Commit**

```bash
git add app/Source/WebViewBridge.cpp ui/src/lib/bridge.ts ui/src/lib/nativeRoutes.generated.ts ui/src/workspace/Mixer.tsx ui/src/workspace/Mixer.test.tsx
git commit -m "feat(inserts): bypass a slot from the mixer strip"
```

---

## Task 2: Drag-reorder inserts

**Files:**
- Modify: `app/Source/WebViewBridge.cpp`
- Modify: `ui/src/lib/bridge.ts`, `ui/src/workspace/Mixer.tsx`
- Test: `ui/src/workspace/Mixer.test.tsx`

**Interfaces:**
- Consumes: `AudioEngine::moveTrackInsert (int track, int from, int to)` — returns `bool`.
- Produces: route `moveInsert(track, from, to) => AppState`; `bridge.moveInsert(track, from, to)`.

- [ ] **Step 1: Write the failing test**

```tsx
it("drops slot 0 onto slot 2 and calls moveInsert", async () => {
  const backend = makeFakeBackend();
  render(<Mixer backend={backend} state={stateWithInserts(0, ["Pro-Q", "Pro-C"])} />);
  await drag(screen.getByLabelText("Insert 1 Pro-Q"), screen.getByLabelText("Insert 3 empty"));
  expect(backend.moveInsert).toHaveBeenCalledWith(0, 0, 2);
});

it("refuses to drop onto a slot that already holds a plugin", async () => {
  const backend = makeFakeBackend();
  render(<Mixer backend={backend} state={stateWithInserts(0, ["Pro-Q", "Pro-C"])} />);
  await drag(screen.getByLabelText("Insert 1 Pro-Q"), screen.getByLabelText("Insert 2 Pro-C"));
  expect(backend.moveInsert).not.toHaveBeenCalled();
});
```

- [ ] **Step 2: Run it and watch it fail**

Run: `cd ui && npx vitest run src/workspace/Mixer.test.tsx`
Expected: FAIL — no `moveInsert` on the fake backend.

- [ ] **Step 3: Add the route, the guard and the drop handler**

Handler mirrors Task 1's shape, calling `engine.moveTrackInsert (track, from, to)` and answering
`buildState()`. The UI refuses a drop onto an occupied slot before calling the backend (the native
side would also refuse; the UI refusal is what the test pins).

- [ ] **Step 4: Gate** — vitest PASS, native build exit 0, 195/195.
- [ ] **Step 5: Commit** — `feat(inserts): drag to reorder insert slots`.

---

## Task 3: Open a plugin editor from the shell

**Files:**
- Modify: `app/Source/WebViewBridge.cpp`, `app/Source/MainComponent.h` (one member)
- Modify: `ui/src/lib/bridge.ts`, `ui/src/workspace/Mixer.tsx`
- Test: `ui/src/workspace/Mixer.test.tsx`

**Interfaces:**
- Consumes: `nc::PluginEditorWindow (const juce::String& title, juce::AudioProcessor* processor)`;
  `AudioEngine::trackInsertInstance (int track, int slot)`, `trackInstrumentInstance (int track)`,
  `masterInsertInstance()`.
- Produces: route `openPluginEditor(track, slot) => { ok: boolean, error?: string }`; slot `-1` =
  instrument lane, `-2` = master insert, `0..7` = effect slot.

- [ ] **Step 1: Write the failing UI test**

```tsx
it("opens the editor for a loaded slot", async () => {
  const backend = makeFakeBackend({ editorResult: { ok: true } });
  render(<Mixer backend={backend} state={stateWithInsert(0, 0)} />);
  await userEvent.dblClick(screen.getByLabelText("Insert 1 Pro-Q"));
  expect(backend.openPluginEditor).toHaveBeenCalledWith(0, 0);
});

it("surfaces the error when the slot is empty", async () => {
  const backend = makeFakeBackend({ editorResult: { ok: false, error: "no plugin in slot" } });
  render(<Mixer backend={backend} state={stateWithEmptyInsert(0, 0)} />);
  await userEvent.dblClick(screen.getByLabelText("Insert 1 empty"));
  expect(await screen.findByText("no plugin in slot")).toBeInTheDocument();
});
```

- [ ] **Step 2: Run it and watch it fail.**
- [ ] **Step 3: Add the route**

Resolve the processor for the slot; if it is null answer `{ ok: false, error: "no plugin in slot" }`
without touching any window. Otherwise hold one `std::unique_ptr<nc::PluginEditorWindow> editorWindow`
on `MainComponent` and construct it with the same pattern `MainComponent.cpp:1735` uses today.
`PluginEditorWindow` already watches for its processor dying, so replacing the member closes the old
window safely.

- [ ] **Step 4: Gate** — vitest PASS; native build exit 0; `ctest` 195/195 (includes
  `WindowLifetimeTests`, which must stay green).
- [ ] **Step 5: Commit** — `feat(inserts): open a hosted plugin editor from the shell`.

---

## Task 4: Master A/B

**Files:**
- Modify: `app/Source/WebViewBridge.cpp`, `app/Source/MainComponent.h` (one `juce::String` member)
- Modify: `ui/src/lib/bridge.ts`, `ui/src/workspace/HeaderBar.tsx`
- Test: `ui/src/workspace/HeaderBar.test.tsx`

**Interfaces:**
- Consumes: `AudioEngine::masterPluginStateBase64()`, `restoreMasterPlugin (identifier, stateBase64)`.
- Produces: routes `captureMasterAb() => { state: string `}`, `recallMasterAb(state) => AppState`;
  `bridge.captureMasterAb()`, `bridge.recallMasterAb(state)`.

- [ ] **Step 1: Write the failing test**

```tsx
it("captures the master rack and recalls it", async () => {
  const backend = makeFakeBackend({ captured: "BASE64" });
  render(<HeaderBar backend={backend} />);
  await userEvent.click(screen.getByRole("button", { name: "A/B capture" }));
  await userEvent.click(screen.getByRole("button", { name: "A/B recall" }));
  expect(backend.recallMasterAb).toHaveBeenCalledWith("BASE64");
});
```

- [ ] **Step 2: Run it and watch it fail.**
- [ ] **Step 3: Add the routes and the two buttons.** Capture stores the base64 in a
  `juce::String masterAbState` member; recall refuses when the member is empty and reports
  "no master snapshot captured" rather than restoring an empty rack.
- [ ] **Step 4: Gate** — vitest PASS, native build exit 0, 195/195.
- [ ] **Step 5: Commit** — `feat(master): capture and recall a master A/B snapshot`.

---

## Task 5: Automation read toggle (AUTO)

**Files:**
- Modify: `app/Source/AudioEngine.h` (one array + two methods)
- Modify: `app/Source/AudioEngineTransport.cpp` (`automationValue`), `app/Source/AudioEngineRender.cpp:556-575`
- Modify: `app/Source/WebViewBridge.cpp`, `ui/src/lib/bridge.ts`, `ui/src/workspace/Mixer.tsx`
- Test: `app/Source/AutomationTests.cpp`, `ui/src/workspace/Mixer.test.tsx`

**Interfaces:**
- Produces (engine):

```cpp
void setAutomationRead (int track, bool enabled);
bool automationRead (int track) const;
```

backed by `std::atomic<bool> automationReadEnabled[trackCount]`, default `true`.
- Produces (route): `setAutomationRead(track, enabled) => AppState`.

- [ ] **Step 1: Write the failing native test**

```cpp
TEST_CASE ("automation read off pins the fader to its static value", "[automation]")
{
    nc::AudioEngine engine;
    engine.setActiveTrackCount (2);
    engine.setTrackAutomation (0, nc::AutomationParam::Volume,
                               { { 0.0, 0.0f }, { 4.0, 1.0f } });
    CHECK (engine.automationValue (0, nc::AutomationParam::Volume, 4.0) > 0.9f);

    engine.setAutomationRead (0, false);
    CHECK (engine.automationValue (0, nc::AutomationParam::Volume, 4.0) == 0.0f);
}
```

- [ ] **Step 2: Run it and watch it fail**

Run: `build/app/NCMixRackTests_artefacts/Release/NCMixRackTests.exe "[automation]" -s`
Expected: FAIL — `setAutomationRead` does not exist.

- [ ] **Step 3: Implement the gate**

`automationValue` returns `0.0f` when `! automationRead (track)`. In `renderBlock` at the two lane
loads (volume line 567, pan line 571) treat a disabled track the same as a missing lane, so the
block uses the static fader value.

- [ ] **Step 4: Run the native test, then the gate**

`[automation]` → PASS; digest unchanged; `ctest` 195/195 plus the new case.

- [ ] **Step 5: Add the route and the strip button**, with a vitest covering that the button calls
  `setAutomationRead(track, !enabled)`.

- [ ] **Step 6: Commit** — `feat(automation): per-track read toggle`.

---

## Self-Review

- Every roadmap Phase 2 item has a task: bypass (1), editor window (3), drag-reorder (2), master A/B
  (4), AUTO (5).
- Review Focus lines map to tests: bypass+editor is covered because the editor task keeps the window
  alive while Task 1 flips audio; the reorder-onto-occupied refusal is Task 2's second test; empty
  slot is Task 3's second test; AUTO-off is Task 5 Step 1; missing-plugin A/B is Task 4 Step 3's
  refusal path.
- Interface names are consistent across tasks: `setInsertBypassed`, `moveInsert`,
  `openPluginEditor`, `captureMasterAb`, `recallMasterAb`, `setAutomationRead`.
