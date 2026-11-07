#include "AuxBus.h"

#include <cmath>

namespace nc
{
struct AuxBus::Impl
{
    // Schroeder reverb: 4 parallel combs (feedback) + 2 series allpasses.
    // Prime-ish delay lengths in samples (at 44.1k, rounded per rate).
    std::vector<std::vector<float>> comb;
    std::vector<int> combWrite;
    std::vector<std::vector<float>> allpass;
    std::vector<int> allpassWrite;
    std::vector<int> combDelay;
    std::vector<int> allpassDelay;

    // Stereo delay: per-channel circular buffer + one-pole feedback lowpass.
    std::vector<std::vector<float>> delay;
    std::vector<int> delayWrite;
    std::vector<float> fbLowpass;

    void prepareReverb (double rate)
    {
        const double scale = rate / 44100.0;
        combDelay = { static_cast<int> (1116 * scale), static_cast<int> (1188 * scale),
                      static_cast<int> (1277 * scale), static_cast<int> (1356 * scale) };
        allpassDelay = { static_cast<int> (556 * scale), static_cast<int> (441 * scale) };

        comb.assign (4, {});
        combWrite.assign (4, 0);
        for (int i = 0; i < 4; ++i)
            comb[static_cast<size_t> (i)].assign (static_cast<size_t> (juce::jmax (1, combDelay[static_cast<size_t> (i)])), 0.0f);

        allpass.assign (2, {});
        allpassWrite.assign (2, 0);
        for (int i = 0; i < 2; ++i)
            allpass[static_cast<size_t> (i)].assign (static_cast<size_t> (juce::jmax (1, allpassDelay[static_cast<size_t> (i)])), 0.0f);
    }

    void prepareDelay (double rate)
    {
        // Dotted 1/8 at 120 BPM = 375ms. Fixed time so the bus is stable on
        // devices without a transport-locked clock contribution (tempo-synced
        // 1/8 is the documented default; hosts can retune later).
        const auto samples = juce::jmax (1, static_cast<int> (rate * 0.375));
        delay.assign (2, std::vector<float> (static_cast<size_t> (samples), 0.0f));
        delayWrite.assign (2, 0);
        fbLowpass.assign (2, 0.0f);
    }

    void resetAll()
    {
        for (auto& line : comb)
            std::fill (line.begin(), line.end(), 0.0f);
        for (auto& line : allpass)
            std::fill (line.begin(), line.end(), 0.0f);
        for (auto& line : delay)
            std::fill (line.begin(), line.end(), 0.0f);
        std::fill (fbLowpass.begin(), fbLowpass.end(), 0.0f);
        std::fill (combWrite.begin(), combWrite.end(), 0);
        std::fill (allpassWrite.begin(), allpassWrite.end(), 0);
        std::fill (delayWrite.begin(), delayWrite.end(), 0);
    }

    float processComb (int index, float input)
    {
        auto& line = comb[static_cast<size_t> (index)];
        auto& w = combWrite[static_cast<size_t> (index)];
        const float delayed = line[static_cast<size_t> (w)];
        line[static_cast<size_t> (w)] = input + delayed * 0.84f;
        w = (w + 1) % static_cast<int> (line.size());
        return delayed;
    }

    float processAllpass (int index, float input)
    {
        auto& line = allpass[static_cast<size_t> (index)];
        auto& w = allpassWrite[static_cast<size_t> (index)];
        const float delayed = line[static_cast<size_t> (w)];
        const float out = -input * 0.5f + delayed;
        line[static_cast<size_t> (w)] = input + delayed * 0.5f;
        w = (w + 1) % static_cast<int> (line.size());
        return out;
    }

    float processDelay (int channel, float input)
    {
        auto& line = delay[static_cast<size_t> (channel)];
        auto& w = delayWrite[static_cast<size_t> (channel)];
        const float delayed = line[static_cast<size_t> (w)];
        float& lp = fbLowpass[static_cast<size_t> (channel)];
        lp += 0.25f * (delayed - lp);
        line[static_cast<size_t> (w)] = input + lp * 0.42f;
        w = (w + 1) % static_cast<int> (line.size());
        return delayed;
    }
};

AuxBus::AuxBus() : impl (std::make_unique<Impl>()) {}

AuxBus::~AuxBus() = default;

void AuxBus::prepare (double sampleRate, int maxBlockSize, Kind kindToUse)
{
    kind = kindToUse;
    sampleRate = sampleRate > 0.0 ? sampleRate : 48000.0;
    maxBlock = juce::jmax (1, maxBlockSize);
    impl->resetAll();

    if (kind == Kind::reverb)
        impl->prepareReverb (sampleRate);
    else
        impl->prepareDelay (sampleRate);

    prepared = true;
}

void AuxBus::reset()
{
    if (! prepared)
        return;

    impl->resetAll();
}

void AuxBus::process (juce::AudioBuffer<float>& buffer, int numSamples)
{
    if (! prepared || buffer.getNumChannels() == 0 || numSamples <= 0)
        return;

    const auto numChannels = juce::jmin (buffer.getNumChannels(), 2);

    for (int sample = 0; sample < numSamples; ++sample)
    {
        for (int channel = 0; channel < numChannels; ++channel)
        {
            float input = buffer.getSample (channel, sample);
            float output = input;

            if (kind == Kind::reverb)
            {
                float combSum = 0.0f;
                for (int i = 0; i < 4; ++i)
                    combSum += impl->processComb (i, input);

                float wet = combSum * 0.25f;
                wet = impl->processAllpass (0, wet);
                wet = impl->processAllpass (1, wet);
                output = wet;
            }
            else
            {
                output = impl->processDelay (channel, input);
            }

            // NaN guard: a hostile send can't poison the master bus.
            if (! std::isfinite (output))
                output = 0.0f;

            buffer.setSample (channel, sample, output);
        }
    }
}
} // namespace nc
