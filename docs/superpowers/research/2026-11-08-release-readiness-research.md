# Research — release readiness, 8 November 2026

Findings that change what the plan says, each tied to a source. Everything here
was read while writing `docs/superpowers/plans/2026-11-08-release-completion/PLAN.md`;
nothing in this file is a claim about this codebase.

## 1. Plug-in validation

- pluginval runs headless and returns **0 pass / 1 fail**, so it can gate CI; it
  validates in a separate process so a crash is a failure rather than a hung job.
  `--strictness-level` is 1–10 and **5 is the accepted floor for host
  compatibility**; higher levels add parameter fuzzing and repeated state
  restoration and take much longer.
  ([README](https://github.com/Tracktion/pluginval),
  [Adding pluginval to CI](https://github.com/Tracktion/pluginval/blob/develop/docs/Adding%20pluginval%20to%20CI.md))
- The GitHub action form is `Tracktion/pluginval/validate@v2` with `plugins`,
  `strictness-level` and `args: --skip-gui-tests`. **Level ≥ 8 has a known
  false positive**: "Discrete parameter state restoration" fails for plug-ins
  using `juce::AudioParameterBool` until JUCE snaps the value to 0/1, and the
  documented workaround is the `DISABLED_TESTS` env var.
  ([actions README](https://github.com/Tracktion/pluginval/blob/develop/.github/actions/README.md))
- Steinberg's `vstvalidator` is embedded in pluginval builds (levels 5+) rather
  than run separately, so one tool covers both VST3 conformance and host-API
  robustness. ([CLAUDE.md](https://github.com/Tracktion/pluginval/blob/develop/CLAUDE.md))
- `pluginval test` (acceptance mode) compares a deterministic render against a
  reference file and exits non-zero on mismatch — this is the only practical
  guard against silent DSP regressions.
  ([Acceptance testing](https://github.com/Tracktion/pluginval/blob/develop/docs/Acceptance%20testing.md))

## 2. Code signing on Windows

- Sign with `signtool` and **always timestamp with RFC 3161**
  (`/tr <url> /td SHA256`); an untimestamped signature stops validating when the
  certificate expires, a timestamped one keeps validating.
  ([signtool](https://learn.microsoft.com/en-us/windows/win32/seccrypto/signtool),
  [MSIX signing overview](https://learn.microsoft.com/en-us/windows/msix/package/signing-package-overview))
- **EV certificates no longer bypass SmartScreen** — that behaviour was removed
  in 2024; EV and OV now build reputation the same way. Azure Artifact Signing
  (formerly Trusted Signing) is ~$9.99/month, available to organisations in
  USA/Canada/EU/UK and to individuals in USA/Canada, and is the cheapest route
  to a consistently signed, reputation-accumulating publisher.
  ([Code signing options](https://learn.microsoft.com/en-us/windows/apps/package-and-deploy/code-signing-options))
- Reputation accrues per signing identity across releases, so the same
  certificate must sign every build rather than a fresh one per version.

## 3. Installers for audio plug-ins

- **Inno Setup** (.iss script) is the friendlier default; **NSIS** is lower level
  and more scriptable. Both are mature and standard for plug-ins.
- The installer must copy the **whole `.vst3` bundle directory**, not the inner
  DLL — copying the file alone produces a plug-in that scans but never loads.
  ([PluginVault: how to make an audio plug-in installer](https://pluginvault.app/blog/how-to-make-an-audio-plugin-installer))
- Unsigned installers trigger a SmartScreen "unknown publisher" prompt, which is
  the single largest drop-off point for first-time installs.

## 4. Licensing (blocking, not technical)

- JUCE 8 is **dual-licensed: AGPLv3 or the commercial JUCE licence**. Distributing
  a *closed-source* binary containing JUCE requires a paid JUCE licence, and the
  JUCE 8 EULA makes the **product owner** responsible for that — contractors no
  longer carry it. ([JUCE licensing FAQ](https://juce.com/get-juce),
  [JUCE 8 EULA amendments](https://forum.juce.com/t/amendments-to-the-juce-end-user-licence-agreement-for-juce-8/61265))
- Fonts are a separate exposure: Monotype's terms grant *document* embedding
  rights, and Microsoft's own font FAQ states explicitly that **document
  embedding permissions do not extend to embedding in an application**. The two
  fonts committed in `ui/src/fonts/` (DIN Neuzeit Light, Motter Corpus
  Condensed) are Monotype faces, so shipping them inside the binary needs an
  app-embedding licence or a swap to an OFL face.
  ([Monotype terms](https://www.monotype.com/terms-and-conditions),
  [Microsoft font FAQ](https://learn.microsoft.com/en-us/typography/fonts/font-faq))

## 5. Pinning the out-of-repo dependency

- CMake advises a **commit hash** rather than a tag or branch for `GIT_TAG` when
  you do not control the remote: tags can be moved, hashes cannot.
  ([FetchContent](https://cmake.org/cmake/help/v3.30/module/FetchContent.html))
- CPM says the same and supports `gh:user/repo#<commit>` directly.
  ([CPM.cmake README](https://github.com/cpm-cmake/CPM.cmake/blob/master/README.md))
- For offline/clean builds, `FETCHCONTENT_SOURCE_DIR_<NAME>` points at a local
  checkout; `FETCHCONTENT_FULLY_DISCONNECTED` is not usable on a first configure.
  ([CMake Discourse](https://discourse.cmake.org/t/cmake-offline-build-how-to-pre-populate-source-dependencies-in-the-build-tree/13173))

## 6. High-DPI with a WebView UI

- JUCE 8.0.13 changed Windows DPI handling; hosts that both mark the window
  DPI-aware **and** send `IPlugViewContentScaleSupport::setContentScaleFactor`
  get the scale applied twice (seen in Renoise at 125%), so 150% must be tested
  per host, not once.
  ([JUCE forum: DPI scaling broken in 8.0.13](https://forum.juce.com/t/windows-dpi-scaling-broken-in-juce-8-0-13-for-some-daws-renoise/68923))
- WebView2 fails to initialise when its DPI-awareness does not match the host
  process ("mismatch in DPI awareness"), and it historically ignored monitor DPI
  changes until a resize. Both are engine-level, so the plan tests the pinned
  JUCE version rather than patching around it.
  ([DPI + WebView thread](https://forum.juce.com/t/dpi-scaling-not-working-on-webview-gui/66971),
  [WebView2 ignores DPI change](https://forum.juce.com/t/bug-webbrowsercomponent-webview2-backend-ignores-dpi-change/58923))

## What this research does not settle

Costs and lead times (certificate, JUCE licence, font licences), whether
`ChordStudio-master` can be published as a dependency at all, and whether the
target hosts behave like Renoise or like Reaper. Those are decisions for the
owner, and the plan marks them as gates rather than tasks an agent can finish.
