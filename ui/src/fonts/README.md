# Workspace typefaces

The Monotype faces this directory used to hold — DIN Neuzeit Grotesk Light and
Motter Corpus Condensed — were removed on 2026-11-12. They were rebuilt subsets
of a mixed freeware/shareware collection, the font files declared no licence,
and neither a document nor a desktop licence covers embedding them in a
distributed build.

The workspace now uses the `@fontsource` families declared in `ui/package.json`:

| Role                       | Family           | Licence     |
| -------------------------- | ---------------- | ----------- |
| UI, and the display lockup | Archivo Variable | SIL OFL 1.1 |
| Every readout              | Share Tech Mono  | SIL OFL 1.1 |

See `THIRD-PARTY-NOTICES.md` at the repository root for the full record.

If the premium faces are licensed later, drop the TTFs back here, restore the
`@font-face` rule in `../brand.css`, and add the licence to the notices.
