# Real audio corpus

These fixtures are short, mono 48 kHz PCM WAV excerpts from real recordings. They
are committed so the quality tests do not download or synthesize audio at test
time.

## Sources and licenses

- `real/piano_a4.wav` — excerpt of `Piano.mf.A4.aiff` from the University of Iowa
  Electronic Music Studios Musical Instrument Samples (MIS), recorded by
  Lawrence Fritts. The MIS page states that the recordings are "freely available"
  and "may be downloaded and used for any projects, without restrictions":
  https://theremin.music.uiowa.edu/mis.html
- `real/piano_c5.wav` — excerpt of `Piano.mf.C5.aiff` from the same collection.
- `real/speech_deana.wav` — trimmed excerpt of
  `LL-Q9027 (swe)-Moonhouse-Deana.wav` from Wikimedia Commons / Lingua Libre,
  dedicated to the public domain under CC0 1.0:
  https://commons.wikimedia.org/wiki/File:LL-Q9027_(swe)-Moonhouse-Deana.wav

The excerpts were trimmed at the first sample above 1% of peak, resampled to
48 kHz, mixed to mono, and normalized only when the source peak exceeded 1.0.
No other processing was applied.

Expected labels live in `real/manifest.tsv`; tests use the same values.
