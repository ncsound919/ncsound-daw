# Human & Hardware Release Gates

This document formalizes the release verification gates that require human evaluation and physical hardware before production binary sign-off. Automated CI and CTest suites guarantee bit-exact signal integrity and crash-free headless logic, but the physical gates below must be exercised by a release engineer.

---

## Gate 1: Physical ASIO / CoreAudio Audio Recording Pass
- **Category:** Audio Capture & Device Drivers
- **Target Department:** Recording (100% closeout)
- **Equipment Needed:** Physical multi-channel audio interface with XLR/line inputs (e.g. Focusrite Scarlett, Universal Audio Apollo, RME Babyface).
- **Procedure:**
  1. Launch `NCSound Music Hub.exe`.
  2. Open Settings -> Audio Devices. Select the ASIO driver and map inputs 1 & 2.
  3. Arm Track 1 for audio recording.
  4. Verify the input level meter responds in real time to input signal.
  5. Hit Record on the transport; perform 3 to 10 seconds of speech or instrumentation.
  6. Stop recording.
- **Acceptance Criteria:**
  - Recorded take appears on the timeline as a valid audio clip.
  - Non-zero peak waveform renders accurately on the clip surface.
  - Playback of the take reproduces the recorded audio without latency drift, glitching, or buffer under-runs (`recordOverflows == 0`).
- **Sign-off Location:** Record date, audio interface model, buffer size, and engineer sign-off in `DAW_WORK_LOG.md`.

---

## Gate 2: Interactive Modal File Chooser Round-Trip
- **Category:** Session Management & OS Integration
- **Target Department:** Session & Export
- **Procedure:**
  1. Create a session with 4 tracks, custom track names, custom fader levels (-4.5 dB), and 2 hosted inserts.
  2. Select `FILE -> Save Session As...`.
  3. Use the native Windows Explorer / macOS file dialog to pick an external directory and name the project `ReleaseCheck.ncproj`.
  4. Save the file. Verify that the shell status displays `SESSION / saved`.
  5. Quit the application completely (`Alt+F4` or window close).
  6. Launch the application and select `FILE -> Open Session...`. Select `ReleaseCheck.ncproj`.
- **Acceptance Criteria:**
  - Session loads completely with identical track count (4), identical names, fader positions, and insert rack configuration.
  - Cancelling the chooser at any point leaves the current session state strictly untouched with status reporting `SESSION / cancelled`.

---

## Gate 3: End-to-End NSIS Windows Installer Verification
- **Category:** Distribution & Setup
- **Target Department:** Packaging & Distribution
- **Prerequisites:** System with `makensis` (NSIS 3.x) installed or configured via `NC_MAKENSIS`.
- **Procedure:**
  1. Run `cpack --config build/CPackConfig.cmake -G NSIS64 -B build/pkg`.
  2. Run the generated installer `ncsound-daw-*.exe` on a clean Windows virtual machine or test machine.
  3. Complete standard installation to `C:\Program Files\NCSound DAW\`.
  4. Verify Start Menu shortcut and installed binaries (`NCSound Music Hub.exe`, VST3 bundles in `Common Files\VST3`, documentation and licenses).
  5. Launch the installed application from the Start Menu shortcut.
  6. Uninstall the application via Windows Add/Remove Programs.
- **Acceptance Criteria:**
  - Installer completes without elevation errors or unhandled file locks.
  - Uninstaller cleanly removes all installed files and directories without leaving residual registry entries.

---

## Gate 4: Hosted Third-Party Plugin Soak Run
- **Category:** Stability & Memory Safety
- **Target Department:** Stability & Performance
- **Procedure:**
  1. On the target production machine, populate the system VST3 directory (`C:\Program Files\Common Files\VST3`) with third-party instruments and effects (e.g. FabFilter, Serum, Kontakt).
  2. Run `node scripts/soak.mjs --minutes 30 --out build/soak-report.txt`.
- **Acceptance Criteria:**
  - All 30 minutes (approx. 168,750 blocks @ 48kHz/512) execute continuously.
  - Zero crashes, zero memory leaks, and zero audio callback buffer overflows (`recordOverflows == 0`).
  - Report saved to `build/soak-report.txt`.

---

## Gate 5: Critical Critical Listening & DSP Load Sanity Pass
- **Category:** Audio Quality & Usability
- **Target Department:** Audio Engine & Usability
- **Procedure:**
  1. Load the full factory demo project with 8 active tracks, synth instruments, and overlay mix racks.
  2. Set playback loop across bars 1 to 32.
  3. Audition playback on reference studio monitors and headphones for 5 uninterrupted minutes.
  4. Monitor CPU / DSP load indicator in the HeaderBar.
- **Acceptance Criteria:**
  - DSP meter remains stable at nominal load (< 0.50).
  - Absolutely zero audio artifacts, clicks, pops, zipper noise on parameter changes, or denormal spikes.
