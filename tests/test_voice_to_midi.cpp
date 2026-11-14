#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <nc/engine/VoiceToMidi.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

using namespace nc::engine;
using Catch::Approx;

namespace
{
constexpr double sr = 48000.0;
constexpr double twoPi = 6.28318530717958647692;

struct Signal
{
    std::vector<float> samples;

    explicit Signal (double seconds)
        : samples (static_cast<std::size_t> (std::llround (seconds * sr)), 0.0f)
    {
    }
};

Signal tone (double seconds, double frequency, double amplitude = 0.35,
             double harmonic = 0.0, double vibratoCents = 0.0, double vibratoHz = 5.0)
{
    Signal signal (seconds);
    double phase = 0.0;

    for (std::size_t i = 0; i < signal.samples.size(); ++i)
    {
        const auto t = static_cast<double> (i) / sr;
        const auto bend = vibratoCents == 0.0
                              ? 1.0
                              : std::pow (2.0, vibratoCents
                                                   * std::sin (twoPi * vibratoHz * t) / 1200.0);
        phase += twoPi * frequency * bend / sr;
        signal.samples[i] = static_cast<float> (amplitude
                                                * (std::sin (phase)
                                                   + harmonic * std::sin (2.0 * phase)));
    }

    return signal;
}

void append (Signal& destination, const Signal& source)
{
    destination.samples.insert (destination.samples.end(),
                                source.samples.begin(), source.samples.end());
}

Signal silence (double seconds)
{
    return Signal (seconds);
}

std::vector<V2MNote> transcribe (const Signal& signal, V2MConfig cfg = {})
{
    cfg.sampleRate = static_cast<int> (sr);
    return voiceToMidi (signal.samples.data(), signal.samples.size(), cfg);
}

int countNote (const std::vector<V2MNote>& notes, int midiNote)
{
    return static_cast<int> (std::count_if (notes.begin(), notes.end(),
                                            [midiNote] (const V2MNote& note)
                                            { return note.midiNote == midiNote; }));
}
} // namespace

TEST_CASE ("steady A440 produces one A4 note", "[voicetomidi]")
{
    const auto notes = transcribe (tone (0.6, 440.0));

    REQUIRE (notes.size() == 1);
    REQUIRE (notes.front().midiNote == 69);
    REQUIRE (notes.front().startBeat == Approx (0.0).margin (0.08));
    REQUIRE (notes.front().lengthBeats > 0.8);
    REQUIRE (notes.front().velocity > 0);
    REQUIRE (notes.front().velocity <= 127);
    REQUIRE (notes.front().confidence > 0.0f);
    REQUIRE (notes.front().confidence <= 1.0f);
}

TEST_CASE ("two separated pitches produce two notes in order", "[voicetomidi]")
{
    Signal signal (0.0);
    append (signal, tone (0.45, 440.0));
    append (signal, silence (0.08));
    append (signal, tone (0.45, 523.251));

    const auto notes = transcribe (signal);

    REQUIRE (notes.size() == 2);
    REQUIRE (notes[0].midiNote == 69);
    REQUIRE (notes[1].midiNote == 72);
    REQUIRE (notes[0].startBeat < notes[1].startBeat);
}

TEST_CASE ("vibrato is one note, not a staircase of fragments", "[voicetomidi]")
{
    const auto notes = transcribe (tone (0.8, 440.0, 0.35, 0.0, 35.0, 5.5), [] {
        V2MConfig cfg;
        cfg.bpm = 120.0;
        return cfg;
    }());

    REQUIRE (notes.size() == 1);
    REQUIRE (notes.front().midiNote == 69);
}

TEST_CASE ("a strong second harmonic does not double the octave", "[voicetomidi]")
{
    const auto notes = transcribe (tone (0.6, 220.0, 0.35, 0.8));

    REQUIRE (notes.size() == 1);
    REQUIRE (notes.front().midiNote == 57);
}

TEST_CASE ("pending pitch frames do not inflate the previous note", "[voicetomidi]")
{
    Signal joined (0.0);
    append (joined, tone (0.45, 440.0, 0.03));
    append (joined, tone (0.45, 523.251, 0.9));

    const auto notes = transcribe (joined);
    const auto isolated = transcribe (tone (0.45, 440.0, 0.03));

    REQUIRE (isolated.size() == 1);
    REQUIRE (notes.size() >= 2);
    REQUIRE (notes[0].midiNote == 69);
    REQUIRE (notes[1].midiNote == 72);
    CAPTURE (notes[0].velocity, isolated[0].velocity,
             notes[0].confidence, isolated[0].confidence);
    REQUIRE (notes[0].velocity <= isolated[0].velocity + 2);
    REQUIRE (notes[0].confidence <= isolated[0].confidence + 0.02f);
}

TEST_CASE ("pitch bends follow vibrato and return to centre", "[voicetomidi]")
{
    const auto notes = transcribe (tone (0.8, 440.0, 0.35, 0.0, 50.0, 5.5));

    REQUIRE (notes.size() == 1);
    const auto& bends = notes.front().bends;
    REQUIRE (bends.size() >= 3);

    auto lowest = 8192;
    auto highest = 8192;

    for (const auto& bend : bends)
    {
        REQUIRE (bend.beatOffset >= 0.0);
        REQUIRE (bend.beatOffset <= notes.front().lengthBeats + 1e-9);
        REQUIRE (bend.value >= 1);
        REQUIRE (bend.value <= 16383);
        lowest = std::min (lowest, bend.value);
        highest = std::max (highest, bend.value);
    }

    REQUIRE (lowest < 8192);
    REQUIRE (highest > 8192);
    REQUIRE (bends.back().value == 8192);
}

TEST_CASE ("unchanged pitch bends are deduplicated", "[voicetomidi]")
{
    const auto notes = transcribe (tone (0.8, 440.0));

    REQUIRE (notes.size() == 1);
    const auto& bends = notes.front().bends;
    REQUIRE (bends.size() < 12);
    REQUIRE (std::adjacent_find (bends.begin(), bends.end(),
                                 [] (const auto& a, const auto& b)
                                 { return a.value == b.value; })
             == bends.end());
}

TEST_CASE ("a short silent gap is bridged, a long one is not", "[voicetomidi]")
{
    Signal shortGap (0.0);
    append (shortGap, tone (0.35, 440.0));
    append (shortGap, silence (0.015));
    append (shortGap, tone (0.35, 440.0));

    const auto bridged = transcribe (shortGap);
    REQUIRE (bridged.size() == 1);
    REQUIRE (bridged.front().midiNote == 69);

    Signal longGap (0.0);
    append (longGap, tone (0.35, 440.0));
    append (longGap, silence (0.18));
    append (longGap, tone (0.35, 440.0));

    const auto split = transcribe (longGap);
    REQUIRE (split.size() == 2);
    REQUIRE (countNote (split, 69) == 2);
}

TEST_CASE ("silence and noise produce no notes", "[voicetomidi]")
{
    REQUIRE (transcribe (silence (0.5)).empty());

    Signal noise (0.5);
    std::uint32_t state = 0x12345678u;

    for (auto& sample : noise.samples)
    {
        state = state * 1664525u + 1013904223u;
        sample = static_cast<float> ((state >> 8) / 8388608.0 - 1.0);
    }

    REQUIRE (transcribe (noise).empty());
}

TEST_CASE ("velocity follows level", "[voicetomidi]")
{
    const auto loud = transcribe (tone (0.5, 440.0, 0.8));
    const auto quiet = transcribe (tone (0.5, 440.0, 0.05));

    REQUIRE (! loud.empty());
    REQUIRE (! quiet.empty());
    REQUIRE (loud.front().velocity > quiet.front().velocity);
}

TEST_CASE ("a zero analysis window is clamped instead of producing NaNs", "[voicetomidi]")
{
    V2MConfig cfg;
    cfg.window = 0;
    cfg.hop = 0;

    const auto notes = transcribe (tone (0.5, 440.0), cfg);

    REQUIRE (! notes.empty());

    for (const auto& note : notes)
    {
        REQUIRE (std::isfinite (note.startBeat));
        REQUIRE (std::isfinite (note.lengthBeats));
        REQUIRE (note.lengthBeats > 0.0);
        REQUIRE (std::isfinite (note.confidence));
    }
}
