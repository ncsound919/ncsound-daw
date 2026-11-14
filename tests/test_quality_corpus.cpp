#include <catch2/catch_test_macros.hpp>

#include <nc/engine/SlicePlanner.hpp>
#include <nc/engine/VoiceToMidi.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

// Deterministic labeled fixtures; these keep the quality checks runnable
// without external assets. Add real recordings to the same case lists as the
// corpus grows.
namespace
{
constexpr int sampleRate = 48000;
constexpr double twoPi = 6.28318530717958647692;

struct OnsetCase
{
    std::string name;
    std::vector<float> audio;
    std::vector<std::int64_t> truth;
};

struct ExpectedNote
{
    int midi = 60;
    double startSeconds = 0.0;
    double endSeconds = 0.0;
};

struct VoiceCase
{
    std::string name;
    std::vector<float> audio;
    std::vector<ExpectedNote> truth;
};

struct Metric
{
    int truePositives = 0;
    int falsePositives = 0;
    int falseNegatives = 0;

    double precision() const
    {
        const auto denominator = truePositives + falsePositives;
        return denominator == 0 ? 1.0 : static_cast<double> (truePositives) / denominator;
    }

    double recall() const
    {
        const auto denominator = truePositives + falseNegatives;
        return denominator == 0 ? 1.0 : static_cast<double> (truePositives) / denominator;
    }

    double f1() const
    {
        const auto p = precision();
        const auto r = recall();
        return p + r == 0.0 ? 0.0 : 2.0 * p * r / (p + r);
    }
};

std::uint32_t nextNoise (std::uint32_t& state)
{
    state = state * 1664525u + 1013904223u;
    return state;
}

double smoothedEnvelope (double t, double duration, double attack, double release)
{
    if (t < 0.0 || t >= duration)
        return 0.0;

    const auto up = attack <= 0.0 ? 1.0 : std::min (1.0, t / attack);
    const auto down = release <= 0.0 ? 1.0 : std::min (1.0, (duration - t) / release);
    return up * down;
}

void addKick (std::vector<float>& audio, int at, float amplitude)
{
    constexpr double duration = 0.24;
    const auto count = static_cast<int> (duration * sampleRate);
    double phase = 0.0;

    for (int i = 0; i < count && at + i < static_cast<int> (audio.size()); ++i)
    {
        const auto t = static_cast<double> (i) / sampleRate;
        const auto frequency = 48.0 + 110.0 * std::exp (-t * 28.0);
        phase += twoPi * frequency / sampleRate;
        const auto envelope = std::exp (-t * 18.0) * smoothedEnvelope (t, duration, 0.002, 0.01);
        audio[static_cast<std::size_t> (at + i)] += amplitude * static_cast<float> (std::sin (phase) * envelope);
    }
}

void addSnare (std::vector<float>& audio, int at, float amplitude)
{
    constexpr double duration = 0.18;
    const auto count = static_cast<int> (duration * sampleRate);
    std::uint32_t noise = static_cast<std::uint32_t> (at * 2654435761u + 17u);
    double phase = 0.0;

    for (int i = 0; i < count && at + i < static_cast<int> (audio.size()); ++i)
    {
        const auto t = static_cast<double> (i) / sampleRate;
        const auto n = static_cast<double> (nextNoise (noise) >> 8) / 8388608.0 - 1.0;
        phase += twoPi * 185.0 / sampleRate;
        const auto envelope = std::exp (-t * 24.0) * smoothedEnvelope (t, duration, 0.001, 0.01);
        audio[static_cast<std::size_t> (at + i)] += amplitude
            * static_cast<float> ((0.75 * n + 0.25 * std::sin (phase)) * envelope);
    }
}

void addHat (std::vector<float>& audio, int at, float amplitude)
{
    constexpr double duration = 0.08;
    const auto count = static_cast<int> (duration * sampleRate);
    std::uint32_t noise = static_cast<std::uint32_t> (at * 2246822519u + 29u);
    double previous = 0.0;

    for (int i = 0; i < count && at + i < static_cast<int> (audio.size()); ++i)
    {
        const auto t = static_cast<double> (i) / sampleRate;
        const auto n = static_cast<double> (nextNoise (noise) >> 8) / 8388608.0 - 1.0;
        const auto highPassed = n - previous;
        previous = n;
        const auto envelope = std::exp (-t * 55.0) * smoothedEnvelope (t, duration, 0.0005, 0.005);
        audio[static_cast<std::size_t> (at + i)] += amplitude * static_cast<float> (highPassed * envelope);
    }
}

void addBass (std::vector<float>& audio, int at, float amplitude)
{
    constexpr double duration = 0.3;
    const auto count = static_cast<int> (duration * sampleRate);
    double phase = 0.0;

    for (int i = 0; i < count && at + i < static_cast<int> (audio.size()); ++i)
    {
        const auto t = static_cast<double> (i) / sampleRate;
        phase += twoPi * 72.0 / sampleRate;
        const auto envelope = std::exp (-t * 7.0) * smoothedEnvelope (t, duration, 0.004, 0.02);
        audio[static_cast<std::size_t> (at + i)] += amplitude * static_cast<float> (std::sin (phase) * envelope);
    }
}

void addVocal (std::vector<float>& audio, int at, float amplitude)
{
    constexpr double duration = 0.45;
    const auto count = static_cast<int> (duration * sampleRate);
    double phase = 0.0;

    for (int i = 0; i < count && at + i < static_cast<int> (audio.size()); ++i)
    {
        const auto t = static_cast<double> (i) / sampleRate;
        const auto bend = std::pow (2.0, 28.0 * std::sin (twoPi * 5.0 * t) / 1200.0);
        phase += twoPi * 220.0 * bend / sampleRate;
        const auto envelope = smoothedEnvelope (t, duration, 0.025, 0.04);
        const auto sample = std::sin (phase) + 0.45 * std::sin (2.0 * phase)
                            + 0.18 * std::sin (3.0 * phase);
        audio[static_cast<std::size_t> (at + i)] += amplitude * static_cast<float> (sample * envelope);
    }
}

OnsetCase makeOnsetCase (std::string name, double seconds,
                         const std::vector<std::pair<double, int>>& hits,
                         const std::vector<int>& kinds)
{
    OnsetCase result;
    result.name = std::move (name);
    result.audio.assign (static_cast<std::size_t> (seconds * sampleRate), 0.0f);

    for (std::size_t i = 0; i < hits.size(); ++i)
    {
        const auto at = static_cast<int> (hits[i].first * sampleRate);
        result.truth.push_back (at);

        switch (kinds[i])
        {
            case 0: addKick (result.audio, at, 0.8f); break;
            case 1: addSnare (result.audio, at, 0.6f); break;
            case 2: addHat (result.audio, at, 0.45f); break;
            case 3: addBass (result.audio, at, 0.7f); break;
            case 4: addVocal (result.audio, at, 0.5f); break;
            default: break;
        }
    }

    return result;
}

Metric scoreOnsets (const std::vector<std::int64_t>& truth,
                    const std::vector<std::int64_t>& detected,
                    std::int64_t tolerance)
{
    Metric metric;
    std::vector<bool> used (detected.size(), false);

    for (const auto expected : truth)
    {
        auto best = -1;
        auto bestDistance = tolerance + 1;

        for (std::size_t i = 0; i < detected.size(); ++i)
        {
            if (used[i])
                continue;

            const auto distance = std::llabs (detected[i] - expected);

            if (distance < bestDistance)
            {
                bestDistance = distance;
                best = static_cast<int> (i);
            }
        }

        if (best >= 0)
        {
            used[static_cast<std::size_t> (best)] = true;
            ++metric.truePositives;
        }
        else
        {
            ++metric.falseNegatives;
        }
    }

    for (const auto wasUsed : used)
        if (! wasUsed)
            ++metric.falsePositives;

    return metric;
}

std::vector<OnsetCase> onsetCorpus()
{
    std::vector<OnsetCase> cases;

    cases.push_back (makeOnsetCase ("kick pattern", 6.0,
                                    { { 0.5, 0 }, { 1.5, 0 }, { 2.5, 0 }, { 3.5, 0 }, { 4.5, 0 } },
                                    { 0, 0, 0, 0, 0 }));
    cases.push_back (makeOnsetCase ("snare and hats", 5.0,
                                    { { 0.5, 1 }, { 1.0, 2 }, { 1.5, 2 }, { 2.5, 1 }, { 3.0, 2 } },
                                    { 1, 2, 2, 1, 2 }));
    cases.push_back (makeOnsetCase ("bass line", 4.0,
                                    { { 0.5, 3 }, { 1.5, 3 }, { 2.5, 3 } },
                                    { 3, 3, 3 }));
    cases.push_back (makeOnsetCase ("vocal phrase", 5.0,
                                    { { 0.5, 4 }, { 2.0, 4 }, { 3.5, 4 } },
                                    { 4, 4, 4 }));
    cases.push_back (makeOnsetCase ("quiet after loud", 3.0,
                                    { { 0.5, 0 }, { 2.0, 1 } },
                                    { 0, 1 }));

    OnsetCase buried;
    buried.name = "quiet hit under pad";
    buried.audio.assign (static_cast<std::size_t> (4.0 * sampleRate), 0.0f);

    for (std::size_t i = 0; i < buried.audio.size(); ++i)
    {
        const auto t = static_cast<double> (i) / sampleRate;
        buried.audio[i] = static_cast<float> (0.12
                                             * (std::sin (twoPi * 110.0 * t)
                                                + 0.5 * std::sin (twoPi * 220.0 * t)
                                                + 0.25 * std::sin (twoPi * 330.0 * t)));
    }

    addSnare (buried.audio, static_cast<int> (2.0 * sampleRate), 0.035f);
    buried.truth = { 0, static_cast<std::int64_t> (2.0 * sampleRate) };
    cases.push_back (std::move (buried));

    OnsetCase noisy;
    noisy.name = "noise floor with hats";
    noisy.audio.assign (static_cast<std::size_t> (4.0 * sampleRate), 0.0f);
    std::uint32_t noise = 0x12345678u;

    for (auto& sample : noisy.audio)
        sample = static_cast<float> (((nextNoise (noise) >> 8) / 8388608.0 - 1.0) * 0.006);

    addHat (noisy.audio, static_cast<int> (0.8 * sampleRate), 0.35f);
    addHat (noisy.audio, static_cast<int> (2.2 * sampleRate), 0.35f);
    noisy.truth = { static_cast<std::int64_t> (0.8 * sampleRate),
                    static_cast<std::int64_t> (2.2 * sampleRate) };
    cases.push_back (std::move (noisy));

    OnsetCase split;
    split.name = "separated hits";
    split.audio.assign (static_cast<std::size_t> (4.0 * sampleRate), 0.0f);
    addKick (split.audio, static_cast<int> (0.6 * sampleRate), 0.8f);
    addHat (split.audio, static_cast<int> (1.8 * sampleRate), 0.5f);
    split.truth = { static_cast<std::int64_t> (0.6 * sampleRate),
                    static_cast<std::int64_t> (1.8 * sampleRate) };
    cases.push_back (std::move (split));

    return cases;
}

VoiceCase makeVoiceCase (std::string name, double seconds)
{
    VoiceCase result;
    result.name = std::move (name);
    result.audio.assign (static_cast<std::size_t> (seconds * sampleRate), 0.0f);
    return result;
}

void addVoiceNote (std::vector<float>& audio, double start, double duration,
                   double frequency, float amplitude, double harmonic,
                   double vibratoCents)
{
    const auto first = static_cast<int> (start * sampleRate);
    const auto count = static_cast<int> (duration * sampleRate);
    double phase = 0.0;

    for (int i = 0; i < count && first + i < static_cast<int> (audio.size()); ++i)
    {
        const auto t = static_cast<double> (i) / sampleRate;
        const auto bend = vibratoCents == 0.0
                              ? 1.0
                              : std::pow (2.0, vibratoCents
                                               * std::sin (twoPi * 5.5 * t) / 1200.0);
        phase += twoPi * frequency * bend / sampleRate;
        const auto envelope = smoothedEnvelope (t, duration, 0.02, 0.03);
        const auto sample = std::sin (phase) + harmonic * std::sin (2.0 * phase);
        audio[static_cast<std::size_t> (first + i)] += amplitude * static_cast<float> (sample * envelope);
    }
}

std::vector<VoiceCase> voiceCorpus()
{
    std::vector<VoiceCase> cases;

    auto steady = makeVoiceCase ("steady A4", 0.8);
    addVoiceNote (steady.audio, 0.0, 0.75, 440.0, 0.35f, 0.0, 0.0);
    steady.truth = { { 69, 0.0, 0.75 } };
    cases.push_back (std::move (steady));

    auto twoNotes = makeVoiceCase ("A4 then C5", 1.3);
    addVoiceNote (twoNotes.audio, 0.0, 0.45, 440.0, 0.35f, 0.0, 0.0);
    addVoiceNote (twoNotes.audio, 0.60, 0.45, 523.251, 0.35f, 0.0, 0.0);
    twoNotes.truth = { { 69, 0.0, 0.45 }, { 72, 0.60, 1.05 } };
    cases.push_back (std::move (twoNotes));

    auto repeat = makeVoiceCase ("repeated A4", 1.2);
    addVoiceNote (repeat.audio, 0.0, 0.42, 440.0, 0.35f, 0.0, 0.0);
    addVoiceNote (repeat.audio, 0.58, 0.42, 440.0, 0.35f, 0.0, 0.0);
    repeat.truth = { { 69, 0.0, 0.42 }, { 69, 0.58, 1.0 } };
    cases.push_back (std::move (repeat));

    auto vibrato = makeVoiceCase ("vibrato A4", 1.0);
    addVoiceNote (vibrato.audio, 0.0, 0.9, 440.0, 0.35f, 0.0, 35.0);
    vibrato.truth = { { 69, 0.0, 0.9 } };
    cases.push_back (std::move (vibrato));

    auto quiet = makeVoiceCase ("quiet A4", 0.8);
    addVoiceNote (quiet.audio, 0.0, 0.7, 440.0, 0.05f, 0.0, 0.0);
    quiet.truth = { { 69, 0.0, 0.7 } };
    cases.push_back (std::move (quiet));

    auto harmonic = makeVoiceCase ("strong second harmonic", 0.8);
    addVoiceNote (harmonic.audio, 0.0, 0.7, 220.0, 0.35f, 0.8, 0.0);
    harmonic.truth = { { 57, 0.0, 0.7 } };
    cases.push_back (std::move (harmonic));

    auto consonant = makeVoiceCase ("same pitch with consonant", 1.2);
    addVoiceNote (consonant.audio, 0.0, 1.0, 440.0, 0.35f, 0.0, 0.0);

    for (auto i = static_cast<int> (0.43 * sampleRate);
         i < static_cast<int> (0.50 * sampleRate); ++i)
        consonant.audio[static_cast<std::size_t> (i)] *= 0.15f;

    consonant.truth = { { 69, 0.0, 0.43 }, { 69, 0.50, 1.0 } };
    cases.push_back (std::move (consonant));

    return cases;
}

Metric scoreNotes (const std::vector<ExpectedNote>& truth,
                   const std::vector<nc::engine::V2MNote>& detected,
                   double bpm)
{
    Metric metric;
    std::vector<bool> used (detected.size(), false);
    constexpr double toleranceSeconds = 0.12;

    for (const auto& expected : truth)
    {
        auto best = -1;
        auto bestDistance = toleranceSeconds + 1.0;

        for (std::size_t i = 0; i < detected.size(); ++i)
        {
            if (used[i] || detected[i].midiNote != expected.midi)
                continue;

            const auto detectedStart = detected[i].startBeat * 60.0 / bpm;
            const auto distance = std::abs (detectedStart - expected.startSeconds);

            if (distance < bestDistance)
            {
                bestDistance = distance;
                best = static_cast<int> (i);
            }
        }

        if (best >= 0)
        {
            used[static_cast<std::size_t> (best)] = true;
            ++metric.truePositives;
        }
        else
        {
            ++metric.falseNegatives;
        }
    }

    for (const auto wasUsed : used)
        if (! wasUsed)
            ++metric.falsePositives;

    return metric;
}
} // namespace

TEST_CASE ("onset quality corpus produces a measurable baseline", "[quality][onset]")
{
    const auto cases = onsetCorpus();
    Metric aggregate;

    for (const auto& fixture : cases)
    {
        const float* channels[] = { fixture.audio.data() };
        const auto detected = nc::detectTransients (channels, 1,
                                                    static_cast<std::int64_t> (fixture.audio.size()),
                                                    0.45);
        const auto metric = scoreOnsets (fixture.truth, detected, sampleRate * 30 / 1000);

        std::cout << "[onset] " << fixture.name
                  << " P=" << metric.precision()
                  << " R=" << metric.recall()
                  << " F1=" << metric.f1()
                  << " detected=" << detected.size()
                  << " truth=" << fixture.truth.size() << '\n';


        if (detected.size() > fixture.truth.size() * 2)
        {
            std::cout << "  positions:";
            for (const auto onset : detected)
                std::cout << " " << (static_cast<double> (onset) / sampleRate);
            std::cout << std::endl;
        }

        aggregate.truePositives += metric.truePositives;
        aggregate.falsePositives += metric.falsePositives;
        aggregate.falseNegatives += metric.falseNegatives;
    }

    std::cout << "[onset] aggregate P=" << aggregate.precision()
              << " R=" << aggregate.recall()
              << " F1=" << aggregate.f1() << '\n';

    REQUIRE_FALSE (cases.empty());
    CHECK (aggregate.f1() >= 0.90);
}

TEST_CASE ("voice quality corpus produces a measurable baseline", "[quality][voicetomidi]")
{
    const auto cases = voiceCorpus();
    Metric aggregate;
    int octaveErrors = 0;

    for (const auto& fixture : cases)
    {
        nc::engine::V2MConfig cfg;
        cfg.sampleRate = sampleRate;
        cfg.bpm = 120.0;

        const auto detected = nc::engine::voiceToMidi (fixture.audio.data(),
                                                       fixture.audio.size(), cfg);
        const auto metric = scoreNotes (fixture.truth, detected, cfg.bpm);

        for (const auto& expected : fixture.truth)
            for (const auto& note : detected)
                if (note.midiNote != expected.midi && note.midiNote % 12 == expected.midi % 12)
                    ++octaveErrors;

        std::cout << "[voice] " << fixture.name
                  << " P=" << metric.precision()
                  << " R=" << metric.recall()
                  << " F1=" << metric.f1()
                  << " detected=" << detected.size()
                  << " truth=" << fixture.truth.size() << '\n';

        aggregate.truePositives += metric.truePositives;
        aggregate.falsePositives += metric.falsePositives;
        aggregate.falseNegatives += metric.falseNegatives;
    }

    std::cout << "[voice] aggregate P=" << aggregate.precision()
              << " R=" << aggregate.recall()
              << " F1=" << aggregate.f1()
              << " octaveErrors=" << octaveErrors << '\n';

    REQUIRE_FALSE (cases.empty());
    CHECK (aggregate.f1() >= 0.90);
    CHECK (octaveErrors <= 1);
}
