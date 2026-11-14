# Third-party notices

What ships in NC Sound Hub's binaries and source tree, and what each licence
requires. This is an engineering record, not legal advice.

## The work as a whole is GPL-3.0

`nc_mixrack` — the application and both plug-in targets — statically links
ChordStudio sources, which are GPL-3.0. The combined binaries are therefore
GPL-3.0: every recipient of a binary is entitled to the corresponding source,
and `LICENSE` (the full GPL-3.0 text) governs distribution. A commercial
ChordStudio licence is the only route to shipping these binaries under other
terms.

## Components

| Component | Version / location | Licence | What it requires |
|---|---|---|---|
| JUCE | `8.0.17-security`, `third_party/JUCE` (pinned in `third_party/JUCE_PIN.txt`) | GPL-3.0, or a commercial JUCE licence | compatible with the GPL-3.0 position above; closed-source distribution would need the commercial licence |
| ChordStudio mix-rack sources | `third_party/chordstudio`, with its own `LICENSE` and `PROVENANCE.md` | GPL-3.0 | source availability for every binary |
| Microsoft WebView2 SDK | resolved from NuGet, see `docs/DEPENDENCIES.md` | Microsoft Edge WebView2 licence terms | runtime distribution follows Microsoft's terms |
| Archivo Variable, Inter, Share Tech Mono | `@fontsource` packages under `ui/` | SIL Open Font License 1.1 | embedding and redistribution allowed; keep the OFL notice |

## Fonts that are no longer distributed

The brand mockups used **DIN Neuzeit Grotesk Light** and **Motter Corpus
Condensed** (Monotype). The copies in `ui/src/fonts/` were rebuilt subsets of a
mixed freeware/shareware collection, the font files declared no licence, and
neither a document nor a desktop licence grants embedding/redistribution
rights. They were removed on 2026-11-12, so no build of this tree embeds them.

`ui/src/brand.css` now sets the display lockup in Archivo Variable with system
fallbacks. If the owner licenses the Monotype faces, drop the files back into
`ui/src/fonts/`, restore the `@font-face` rule in `ui/src/brand.css`, and record
the licence here — the build is complete and distributable without them, just
not in the mockup's display face.
