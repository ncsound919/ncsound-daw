# Workspace typefaces

| File | Source face | Source path |
|---|---|---|
| `din-neuzeit-light.ttf` | DIN Neuzeit Grotesk Light | `fonts/premium/dinneuzeitgrotesk_light_regular.ttf` |
| `motter-corpus-condensed.ttf` | Motter Corpus Condensed ITC TT | `fonts/premium/mottcci.ttf` |

Both are rebuilt subsets (ASCII/Latin + symbols, `PCLT`/`DSIG` dropped) of the
GrowthOS / marketing-studio library originals. DIN Neuzeit's cmap was additionally
rebuilt from its glyph map because Chromium's OTS sanitizer rejected the original
table; without that repair the face silently falls back to Segoe UI.

The library's provenance note (`assets/licenses/GrowthOS-Fonts.md`) records mixed
freeware/shareware collections and that these premium faces declare no licence in
the font file. Distribution terms follow the licence under which the collection
was obtained.
