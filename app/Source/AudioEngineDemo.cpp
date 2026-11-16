// Demo session content: the clips that make Play audible out of the box.
// Split out of AudioEngine.cpp (plan 07-03); body moved verbatim.

#include "AudioEngine.h"
#include "AudioEngineInternal.h"
#include <cmath>

namespace nc
{
void AudioEngine::createDemoAssets()
{
    constexpr double demoRate = 24000.0;
    constexpr double secondsPerBar = 2.0; // 120 BPM, 4/4
    constexpr int demoBars = 16;
    constexpr int frames = static_cast<int> (demoRate * secondsPerBar * demoBars);

    const char* const names[demoTrackCount] {
        "DEMO DRUMS", "DEMO BASS", "DEMO KEYS", "DEMO VOX",
        "DEMO GTR", "DEMO PERC", "DEMO VERB", "DEMO DELAY",
        "DEMO STRINGS", "DEMO PAD", "DEMO LEAD", "DEMO FX1",
        "DEMO FX2", "DEMO BUS", "DEMO AUX3", "DEMO AUX4"
    };

    for (int track = 0; track < demoTrackCount; ++track)
    {
        auto asset = std::make_unique<AudioAsset>();
        asset->sampleRate = demoRate;
        asset->name = names[track];
        asset->buffer.setSize (1, frames);

        auto* data = asset->buffer.getWritePointer (0);
        juce::Random random (0x4e430000 + track * 7919);
        auto noise = [&random] { return random.nextFloat() * 2.0f - 1.0f; };
        auto env = [] (double t, double decay) { return t >= 0.0 ? std::exp (-t / decay) : 0.0; };
        auto note = [] (double base, int semitones) { return base * std::pow (2.0, semitones / 12.0); };

        // One chord per bar, as semitone offsets from A3 (220 Hz). Every
        // harmonic track draws from it so the loops agree with each other.
        static const int chords[4][3] { { 0, 4, 7 }, { 5, 9, 12 }, { 7, 11, 14 }, { 3, 7, 10 } };

        float peak = 0.0f;

        for (int frame = 0; frame < frames; ++frame)
        {
            const auto t = static_cast<double> (frame) / demoRate;
            const auto bar = static_cast<int> (t / secondsPerBar);
            const auto barTime = std::fmod (t, secondsPerBar);
            const auto beat = static_cast<int> (barTime / 0.5);
            const auto beatTime = std::fmod (barTime, 0.5);
            double sample = 0.0;

            switch (track)
            {
                case 0: // drums
                {
                    if (beat == 0 || beat == 2)
                        sample += std::sin (twoPi * (58.0 - 28.0 * beatTime) * beatTime)
                                * env (beatTime, 0.12) * 0.92;

                    if (beat == 1 || beat == 3)
                        sample += static_cast<double> (noise()) * env (beatTime, 0.075) * 0.48;

                    const auto hatTime = std::fmod (beatTime, 0.25);
                    sample += static_cast<double> (noise()) * env (hatTime, 0.018) * 0.20;
                    break;
                }

                case 1: // bass
                {
                    static const int notes[4] { 0, 0, 3, 5 };
                    const auto frequency = note (55.0, notes[bar % 4]);
                    sample = (std::sin (twoPi * frequency * t)
                            + 0.28 * std::sin (twoPi * frequency * 2.0 * t))
                           * env (beatTime, 0.22) * 0.42;
                    break;
                }

                case 2: // keys
                {
                    for (const auto semitone : chords[bar % 4])
                        sample += std::sin (twoPi * note (220.0, semitone) * t) * 0.10;

                    sample *= 0.45 + 0.55 * env (barTime, 0.75);
                    break;
                }

                case 3: // vocal guide
                {
                    static const int melody[4] { 7, 10, 12, 7 };
                    const auto frequency = note (220.0, melody[bar % 4]);
                    const auto vibrato = 1.0 + 0.006 * std::sin (twoPi * 5.2 * t);
                    sample = std::sin (twoPi * frequency * vibrato * t)
                           * (0.38 + 0.28 * std::sin (twoPi * 5.2 * t))
                           * env (barTime, 0.95) * 0.34;
                    break;
                }

                case 4: // guitar
                {
                    static const int riff[4] { 0, 3, 5, 7 };
                    const auto frequency = note (110.0, riff[beat % 4]);
                    sample = (std::sin (twoPi * frequency * t)
                            + 0.35 * std::sin (twoPi * frequency * 2.0 * t))
                           * env (beatTime, 0.13) * 0.32;
                    break;
                }

                case 5: // percussion
                {
                    const auto hatTime = std::fmod (beatTime, 0.25);
                    sample = static_cast<double> (noise()) * env (hatTime, 0.022) * 0.24;
                    break;
                }

                case 6: // plate return guide
                    sample = (std::sin (twoPi * 220.0 * t)
                            + std::sin (twoPi * 277.18 * t)
                            + std::sin (twoPi * 329.63 * t))
                           * 0.035 * (0.55 + 0.45 * std::sin (twoPi * t / 8.0));
                    break;

                case 7: // delay return: the keys chord one bar behind, decaying
                {
                    const auto decay = env (barTime, 0.85) * 0.30;

                    if (decay > 0.002)
                        for (const auto semitone : chords[(bar + 3) % 4])
                            sample += std::sin (twoPi * note (220.0, semitone) * t) * decay;

                    break;
                }

                case 8: // strings: a slow bowed swell on the bar's chord
                {
                    const auto swell = (1.0 - std::exp (-barTime / 0.45))
                                     * env (secondsPerBar - barTime, 0.30);

                    for (const auto semitone : chords[bar % 4])
                    {
                        const auto f = note (220.0, semitone);
                        sample += (std::sin (twoPi * f * t)
                                 + 0.30 * std::sin (twoPi * 2.0 * f * t))
                                * 0.16 * swell;
                    }

                    break;
                }

                case 9: // pad: the chord detuned a few cents, breathing slowly
                {
                    const auto breathe = 0.6 + 0.4 * std::sin (twoPi * t / 8.0);

                    for (const auto semitone : chords[bar % 4])
                    {
                        const auto f = note (220.0, semitone);
                        sample += (std::sin (twoPi * f * t) + std::sin (twoPi * f * 1.004 * t))
                                * 0.10 * breathe;
                    }

                    break;
                }

                case 10: // lead: a pentatonic answer, one note per eighth
                {
                    static const int melody[8] { 12, 15, 17, 15, 12, 10, 7, 10 };
                    const auto eighth = static_cast<int> (barTime / 0.25) % 8;
                    const auto frequency = note (220.0, melody[eighth]);
                    const auto vibrato = 1.0 + 0.004 * std::sin (twoPi * 5.5 * t);

                    sample = std::sin (twoPi * frequency * vibrato * t)
                           * env (std::fmod (barTime, 0.25), 0.35) * 0.30;
                    break;
                }

                case 11: // FX 1: a short rising tone every four bars
                {
                    const auto sweepTime = std::fmod (t, 8.0);

                    if (sweepTime < 0.9)
                    {
                        const auto sweepHz = 220.0 + 660.0 * (sweepTime / 0.9);
                        sample = std::sin (twoPi * sweepHz * t) * env (sweepTime, 0.45) * 0.24;
                    }

                    break;
                }

                case 12: // FX 2: a low impact on the first bar of every four
                {
                    if (bar % 4 == 0)
                        sample = std::sin (twoPi * (70.0 - 30.0 * barTime) * t)
                               * env (barTime, 0.55) * 0.45;

                    break;
                }

                case 13: // bus print: the chord, low and steady
                {
                    for (const auto semitone : chords[bar % 4])
                        sample += std::sin (twoPi * note (220.0, semitone) * t) * 0.06;

                    sample *= 0.7 + 0.3 * env (barTime, 1.2);
                    break;
                }

                case 14: // aux 3 return
                case 15: // aux 4 return
                {
                    for (const auto semitone : chords[bar % 4])
                        sample += std::sin (twoPi * note (110.0, semitone) * barTime) * 0.05;

                    break;
                }

                default:
                    break;
            }

            const auto value = static_cast<float> (sample);
            data[frame] = value;
            peak = juce::jmax (peak, std::abs (value));
        }

        // A demo record needs a mix, not eight full-scale loops. The targets
        // leave headroom for the master and keep the return prints behind the
        // dry tracks.
        constexpr float trackPeaks[demoTrackCount] { 0.50f, 0.35f, 0.25f, 0.22f,
                                                 0.24f, 0.18f, 0.12f, 0.08f,
                                                 0.20f, 0.18f, 0.22f, 0.10f,
                                                 0.10f, 0.15f, 0.08f, 0.08f };

        // The per-track targets balance the loop and already leave the ship
        // headroom: with all sixteen tracks playing at unity the sum (sends
        // included) measures about -2.6 dBFS peak, so the master maximizer
        // rides transients instead of holding the whole mix down. An extra
        // constant trim here is not a substitute for that measurement -- an
        // earlier 0.18 left the demo at -33 dBFS RMS, which reads as silence.
        if (peak > 0.0f)
            for (int frame = 0; frame < frames; ++frame)
                data[frame] = data[frame] / peak * trackPeaks[track];

        const auto demoId = assetLibrary.add (std::move (asset), sampleRate.load());
        assetLibrary.setDemoAssetId (track, demoId);
    }
}

} // namespace nc
