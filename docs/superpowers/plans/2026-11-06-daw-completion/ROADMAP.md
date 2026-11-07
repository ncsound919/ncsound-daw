# NCSound DAW — Completion Roadmap

**Status:** Plan for approval. No implementation has started.
**Date:** 2026-11-06
**Scope:** v1 completion = design-spec milestones **M2–M9**. **M10 (Aero Amp, Open Mic) is explicitly post-v1.**
**Canonical plan for the next phase:** [`PHASE-02-PLAN.md`](./PHASE-02-PLAN.md)

---

## 1. Scope and assumptions

This roadmap completes the DAW in the existing native JUCE application. It does **not** resume the React/WebView architecture described in `docs/superpowers/plans/2026-11-05-ncsound-daw-phase1.md`; that document is historical.

Assumptions:

- Windows 10/11 x64 is the v1 platform. macOS, Linux, AU, ARA and CLAP are out of scope.
- VST3 is the only third-party format for v1. Native/stock modules remain JUCE-native.
- The native UI kit stays. There is no browser-based UI rewrite.
- Third-party inserts are **effects** in the next phase. Instrument hosting and MIDI arrive in Phase 4.
- A project must reopen with its plugin state, even when a plugin is missing. Silently dropping a slot is a data-loss bug.
- Existing uncommitted work must be preserved. Phase 2 begins with a reversible baseline snapshot, not a reset.

“DAW completion” in this roadmap means: record and edit audio, sequence MIDI through hosted instruments, host third-party effects on tracks and master, save/restore all of it, recover from missing plugins, and ship a hardened Windows build. It does not include the M10 amp/mic products.

---

## 2. Current baseline (audited 2026-11-06)

### Working now

- Native JUCE application builds as `NCSound Music Hub.exe` via `build/_verify_mixrack.cmd`.
- 16-track arrange/mix surface, inspector, browser overlay, transport, native mix rack, master rack, channel racks, A/B compare, HPF/LPF, project save/load.
- Master insert can host one external VST3. Plugin state is saved as base64 in the project.
- Native UI audit fixes are in place: readable 132 px mixer strips, scroll range 1–16, live headers, anchored master insert menu, mouse-up rack activation, native `NAT` slots separated from four user slots.
- Test baseline at audit time:
  - `NCMixRackTests`: 51,830 assertions in 6 cases.
  - `nc_tests`: 306 assertions in 66 cases.

### Missing or incomplete

| Area | Current reality | Consequence |
|---|---|---|
| Track inserts | Four static names from `Session.h`; no per-track plugin instances | Browser “load” is a status message, not insertion |
| Track plugin persistence | `ProjectDocument` stores only `masterInsert`; no track slots | A track plugin could not survive save/reopen |
| Scanner | `PluginHost::scanDefaultLocations()` calls `findAllTypesForFile` synchronously in-process | A bad plugin can hang the UI |
| Recording | `AudioEngine` opens the device with `initialiseWithDefaultDevices(0, 2)`; `TransportBar` says record is not wired | No audio input, no takes |
| MIDI / pattern | No note model, pattern page, or MIDI buffer; `PluginHost::processBlock` passes an empty MIDI buffer | No instrument performance or pattern playback |
| Stock suite | Native `MixRack` is baked into tracks; registry entries are not hosted plugin instances | Stock “plugins” cannot be inserted/reordered like third-party effects |
| Hardening | No scanner timeout, NaN containment, pluginval run, hostile-project fuzz, installer, or clean-VM gate | Not releasable |

### Milestone map

| Spec milestone | Status | Evidence / gap |
|---|---|---|
| M0 — build, logo, shell | Done | Native app, typography, build script |
| M1 — engine extraction | Partial | `nc_engine` and tests exist; device/offline render path is app-bound |
| M2 — host + hosting | Partial | Master insert only; no track inserts, no pattern playback |
| M3 — recording | Not started | No input channels, no writer, no comp |
| M4 — UI kit / screens | Largely done | Native arrange/mix/inspector/browser; dead controls still need final sweep |
| M5 — stock FX | Partial | Native rack works; not loadable through the plugin path |
| M6 — instruments | Not started | No MIDI, instrument host, keyboard, or stock instruments |
| M7 — browser tools | Partial | Browser shell and search; scan/load not wired |
| M8 — consolidation | Partial | Native mix rack, duplicate/dead-entry cleanup remains |
| M9 — hardening | Not started | Scanner isolation, fuzz, pluginval, installer, DPI, clean VM |
| M10 — amp/mic | Post-v1 | Aero Amp and Open Mic |

---

## 3. Completion phases

| Phase | Milestones | Goal | Exit gate |
|---|---|---|---|
| **2. Hosted Track Inserts & Session Integrity** | M2 hosting half | Load, process, bypass, remove, save and restore third-party VST3 effects on any of four slots on any track; master uses the same host path; scanner is isolated with a real timeout; project schema v2 | A real VST3 on a track survives save/reopen with state; missing plugin is reported and retained; no allocation/free on the audio callback |
| **3. Recording & Monitoring** | M3 | Open inputs, arm tracks, monitor through inserts, record float WAV, punch, takes/comp, latency compensation, waveforms | Record a 4-bar pass, punch in/out, comp two takes, reopen the project with media intact |
| **4. MIDI, Pattern Page & Instruments** | M4/M6 | MIDI clips and pattern editor, instrument hosting, keyboard, stock instruments, transport sync | Create a pattern, play it through a hosted instrument, save/reopen, edit notes after reload |
| **5. Stock Suite & Browser Consolidation** | M5/M7/M8 | Stock FX/instruments load through the same catalog, sensible default chains, browser tools, remove duplicate/archived entry points | New project starts with stock strip/EQ/comp/delay defaults; browser finds and loads stock and third-party modules; no dead entries |
| **6. Hardening & Release** | M9 | Runtime safety, hostile-project fuzz, pluginval, device loss, 150% DPI, installer, clean-VM test, performance/soak | Release build installs and passes the matrix on a clean Windows VM; 8-hour soak clean |
| **7. Amp & Mic Products** | M10 | Aero Amp, Open Mic, cabinets, mic models | Post-v1 release; not part of the v1 gate |

**M2 caveat:** the design’s M2 exit also mentions a pattern playing. The current app has no MIDI pattern model; that half is delivered in Phase 4. Phase 2 closes the hosting and project half. M2 remains marked partial until Phase 4 is complete.

### Why this order

1. **Hosting before recording.** Monitoring must run through the same insert chain the mix uses. Recording before hosting would need a second monitoring path and then a rewrite.
2. **Recording before MIDI.** The audio path, latency model and project media handling are prerequisites for stable MIDI scheduling and instrument monitoring.
3. **MIDI before stock instruments.** Instruments are only useful once notes can reach them and be saved.
4. **Stock suite after hosting.** Stock modules should use the same insert/catalog contract as third-party VST3s, not a parallel path.
5. **Hardening last.** Fuzzing and pluginval are meaningful only after the data model and RT path stop moving.

---

## 4. Global definition of done (v1)

A build is v1-complete only when all of the following are true:

- [ ] A scanned VST3 effect can be inserted on any of four slots on any track and on the master, and processes audio.
- [ ] Bypass and remove are live and update delay compensation.
- [ ] Track insert name, state and bypass survive save/reopen.
- [ ] A missing plugin is shown by name, retains its saved state, and can be replaced or removed without corrupting the project.
- [ ] Plugin scanning runs out-of-process with a total timeout; a hung plugin cannot freeze or crash the editor.
- [ ] No project-owned allocation, free, lock or file I/O occurs on the audio callback. Third-party plugin internals are outside the host’s control and are documented as such.
- [ ] Recording supports punch, takes/comp and 32-bit float media with correct monitoring latency.
- [ ] MIDI patterns and hosted instruments play in sync and survive reload.
- [ ] Stock suite modules are loadable and usable through the normal catalog.
- [ ] A hostile/truncated project cannot reach the audio thread malformed; non-finite plugin output is contained.
- [ ] `pluginval` (at least strictness 5) passes for first-party plugins, and the app survives a plugin crash without corrupting the project.
- [ ] Installer, 150% DPI, clean-VM and 8-hour soak gates pass.
- [ ] No dead controls remain in the shipping UI.

---

## 5. Risks and mitigations

| Risk | Impact | Mitigation |
|---|---|---|
| Plugin identity is not portable | Project opens with wrong/missing plugin | Store full `PluginDescription` XML plus identifier; resolve by XML fields and fall back to identifier |
| Plugin lifetime vs audio thread | Use-after-free or callback stall | Message-thread ownership, raw-pointer command rail, retire FIFO, no destruction on audio thread |
| Scanner hangs | UI freeze | `KnownPluginList::CustomScanner` + child process with total timeout and `killWorkerProcess()` |
| PDC mismatch | Tracks drift when plugins have latency | Per-track chain latency, `LatencyComp` delays, cap and report over-budget latency |
| Project schema v1 in the wild | Old projects fail | `version 1..2` loadable; v1 gets empty insert slots; save as v2 |
| Existing uncommitted tree | Large refactor loses work | Baseline snapshot/branch after approval; small commits per task |
| Runtime plugin crash | App exits / project loss | Autosave/recovery and crash reporting in Phase 6; scanner isolation does not sandbox processing |
| Native rack order | Inserts sound wrong if placed after fader | Move to standard order: clip → inserts → channel strip → fader/pan → sum |

---

## 6. Phase documents

- **Phase 2 context:** [`PHASE-02-CONTEXT.md`](./PHASE-02-CONTEXT.md)
- **Phase 2 executable plan:** [`PHASE-02-PLAN.md`](./PHASE-02-PLAN.md)
- **Research note:** [`../../research/2026-11-06-daw-completion-research.md`](../../research/2026-11-06-daw-completion-research.md)

The GSD CLI and its planner/checker subagents are not installed in this workspace, and there is no `.planning/ROADMAP.md`. This roadmap uses the repository’s existing `docs/superpowers` convention and applies the GSD gates (validate → research → plan → verify → present) manually. No plan-checker result is claimed.
