#include "AuxBus.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace nc
{
namespace
{
constexpr int combCount = 4;
constexpr int maxAllpassCount = 3;
constexpr int channelCount = 2;
} // namespace

struct AuxBus::Impl
{
    // Reverb: per-channel comb bank feeding a series allpass chain.
    std::vector<std::vector<float>> comb;
    std::vector<int> combWrite;
    std::vector<float> combDamp;

    std::vector<std::vector<float>> allpass;
    std::vector<int> allpassWrite;

    int combsPerChannel = combCount;
    int allpassesPerChannel = 2;
    float combFeedback = 0.84f;
    float dampingCoefficient = 0.0f;

    // Delay: per-channel circular line plus feedback lowpass.
    std::vector<std::vector<float>> delay;
    int delayWrite = 0;
    int delayLength = 1;
    int delayBaseSamples = 1;
    std::vector<float> feedbackLowpass;
    float delayFeedback = 0.42f;
    bool pingPong = false;
    float modulationDepth = 0.0f;
    double lfoPhase = 0.0;
    double flutterPhase = 0.0;

    void clearAll()
    {
        for (auto& line : comb)
            std::fill (line.begin(), line.end(), 0.0f);
        for (auto& line : allpass)
            std::fill (line.begin(), line.end(), 0.0f);
        for (auto& line : delay)
            std::fill (line.begin(), line.end(), 0.0f);

        std::fill (combWrite.begin(), combWrite.end(), 0);
        std::fill (allpassWrite.begin(), allpassWrite.end(), 0);
        std::fill (combDamp.begin(), combDamp.end(), 0.0f);
        std::fill (feedbackLowpass.begin(), feedbackLowpass.end(), 0.0f);
        delayWrite = 0;
        lfoPhase = 0.0;
        flutterPhase = 0.0;
    }

    void prepareReverb (double rate, Engine engine)
    {
        const auto nexus = engine == Engine::nexusReverb;
        const auto scale = rate / 44100.0;

        // Nexus is the long, diffused hall: later comb taps, more feedback,
        // damping in the loop and a third allpass. Overlay is the tighter
        // stock plate.
        int combLengths[combCount] = { 1116, 1188, 1277, 1356 };
        int allpassLengths[maxAllpassCount] = { 556, 441, 0 };

        allpassesPerChannel = 2;
        combFeedback = 0.84f;
        dampingCoefficient = 0.0f;

        if (nexus)
        {
            combLengths[0] = 1687;
            combLengths[1] = 1777;
            combLengths[2] = 1907;
            combLengths[3] = 2087;
            allpassLengths[2] = 341;
            allpassesPerChannel = 3;
            combFeedback = 0.90f;
            dampingCoefficient = 0.30f;
        }

        comb.assign (static_cast<size_t> (channelCount * combCount), {});
        combWrite.assign (static_cast<size_t> (channelCount * combCount), 0);
        combDamp.assign (static_cast<size_t> (channelCount * combCount), 0.0f);

        for (int channel = 0; channel < channelCount; ++channel)
        {
            for (int i = 0; i < combCount; ++i)
            {
                // The right channel is a few samples longer so the hall has
                // width without any modulated delay line.
                const auto spread = nexus && channel == 1 ? 11 : 0;
                const auto length = juce::jmax (8, static_cast<int> (combLengths[i] * scale) + spread);
                const auto index = static_cast<size_t> (channel * combCount + i);
                comb[index].assign (static_cast<size_t> (length), 0.0f);
            }
        }

        allpass.assign (static_cast<size_t> (channelCount * maxAllpassCount), {});
        allpassWrite.assign (static_cast<size_t> (channelCount * maxAllpassCount), 0);

        for (int channel = 0; channel < channelCount; ++channel)
        {
            for (int i = 0; i < allpassesPerChannel; ++i)
            {
                const auto spread = nexus && channel == 1 ? 7 : 0;
                const auto length = juce::jmax (8, static_cast<int> (allpassLengths[i] * scale) + spread);
                const auto index = static_cast<size_t> (channel * maxAllpassCount + i);
                allpass[index].assign (static_cast<size_t> (length), 0.0f);
            }
        }
    }

    void prepareDelay (double rate, Engine engine)
    {
        const auto temporal = engine == Engine::temporalDelay;

        // Dotted 1/8 at 120 BPM = 375 ms. Temporal Delay modulates the read
        // position (tape wow and flutter) and cross-feeds the two channels.
        delayBaseSamples = juce::jmax (8, static_cast<int> (rate * 0.375));
        modulationDepth = temporal ? static_cast<float> (rate * 0.0012) : 0.0f;
        delayFeedback = temporal ? 0.52f : 0.42f;
        pingPong = temporal;

        // Headroom for the modulation swing so the read never wraps twice.
        delayLength = delayBaseSamples + static_cast<int> (rate * 0.01) + 8;
        delay.assign (channelCount, std::vector<float> (static_cast<size_t> (delayLength), 0.0f));
        feedbackLowpass.assign (channelCount, 0.0f);
        delayWrite = 0;
    }

    float readLinear (const std::vector<float>& line, double position) const
    {
        const auto size = static_cast<double> (line.size());

        while (position < 0.0)
            position += size;

        while (position >= size)
            position -= size;

        const auto index = static_cast<int> (position);
        const auto fraction = static_cast<float> (position - index);
        const auto next = (index + 1) % static_cast<int> (line.size());

        return line[static_cast<size_t> (index)]
             + (line[static_cast<size_t> (next)] - line[static_cast<size_t> (index)]) * fraction;
    }

    float processComb (int channel, int index, float input)
    {
        const auto slot = static_cast<size_t> (channel * combCount + index);
        auto& line = comb[slot];
        auto& write = combWrite[slot];
        const auto delayed = line[static_cast<size_t> (write)];

        auto feedback = delayed;

        if (dampingCoefficient > 0.0f)
        {
            combDamp[slot] += dampingCoefficient * (delayed - combDamp[slot]);
            feedback = combDamp[slot];
        }

        line[static_cast<size_t> (write)] = input + feedback * combFeedback;
        write = (write + 1) % static_cast<int> (line.size());
        return delayed;
    }

    float processAllpass (int channel, int index, float input)
    {
        const auto slot = static_cast<size_t> (channel * maxAllpassCount + index);
        auto& line = allpass[slot];
        auto& write = allpassWrite[slot];
        const auto delayed = line[static_cast<size_t> (write)];
        const auto output = -input * 0.5f + delayed;
        line[static_cast<size_t> (write)] = input + delayed * 0.5f;
        write = (write + 1) % static_cast<int> (line.size());
        return output;
    }

    float processReverbFrame (int channel, float input)
    {
        float wet = 0.0f;

        for (int i = 0; i < combsPerChannel; ++i)
            wet += processComb (channel, i, input);

        wet *= 1.0f / static_cast<float> (combCount);

        for (int i = 0; i < allpassesPerChannel; ++i)
            wet = processAllpass (channel, i, wet);

        return wet;
    }

    // One frame of both channels, so the write cursor advances exactly once per
    // frame. Advancing it per channel made the delay time slip a whole block
    // between blocks and turned the return into a block-rate modulated copy.
    void processOverlayDelay (float& left, float& right)
    {
        // Read one delay time behind the write, like the Temporal engine, so the
        // echo lands on the documented 375 ms instead of the whole line length.
        auto readIndex = delayWrite - delayBaseSamples;

        if (readIndex < 0)
            readIndex += delayLength;

        const auto delayedLeft = delay[0][static_cast<size_t> (readIndex)];
        const auto delayedRight = delay[1][static_cast<size_t> (readIndex)];

        feedbackLowpass[0] += 0.25f * (delayedLeft - feedbackLowpass[0]);
        feedbackLowpass[1] += 0.25f * (delayedRight - feedbackLowpass[1]);

        delay[0][static_cast<size_t> (delayWrite)] = left + feedbackLowpass[0] * delayFeedback;
        delay[1][static_cast<size_t> (delayWrite)] = right + feedbackLowpass[1] * delayFeedback;

        delayWrite = (delayWrite + 1) % delayLength;

        left = delayedLeft;
        right = delayedRight;
    }

    void processTemporalDelay (float& left, float& right, const double rate)
    {
        // Tape wow at 0.55 Hz plus a faster flutter term, both scaled in samples.
        lfoPhase += 0.55 / rate;

        if (lfoPhase > 1.0)
            lfoPhase -= 1.0;

        flutterPhase += 6.3 / rate;

        if (flutterPhase > 1.0)
            flutterPhase -= 1.0;

        const auto modulation = modulationDepth
                              * static_cast<float> (std::sin (lfoPhase * 6.283185307179586)
                                                    + 0.35 * std::sin (flutterPhase * 6.283185307179586));

        const auto readPosition = static_cast<double> (delayWrite - delayBaseSamples) - modulation;

        auto readLeft = readLinear (delay[0], readPosition);
        auto readRight = readLinear (delay[1], readPosition);

        // Damp the cross-feedback so repeats darken as they bounce.
        feedbackLowpass[0] += 0.22f * (readLeft - feedbackLowpass[0]);
        feedbackLowpass[1] += 0.22f * (readRight - feedbackLowpass[1]);

        delay[0][static_cast<size_t> (delayWrite)] = left + feedbackLowpass[1] * delayFeedback;
        delay[1][static_cast<size_t> (delayWrite)] = right + feedbackLowpass[0] * delayFeedback;

        delayWrite = (delayWrite + 1) % delayLength;

        left = readLeft;
        right = readRight;
    }
};

const char* AuxBus::engineName (Engine engine) noexcept
{
    switch (engine)
    {
        case Engine::overlayReverb: return "Overlay Reverb";
        case Engine::nexusReverb:   return "Nexus Reverb";
        case Engine::overlayDelay:  return "Overlay Delay";
        case Engine::temporalDelay: return "Temporal Delay";
    }

    return "Overlay Reverb";
}

AuxBus::AuxBus() : impl (std::make_unique<Impl>()) {}

AuxBus::~AuxBus() = default;

void AuxBus::prepare (double rate, int maxBlockSize, Engine engineToUse)
{
    engine = engineToUse;
    sampleRate = rate > 0.0 ? rate : 48000.0;
    maxBlock = juce::jmax (1, maxBlockSize);
    prepared = false;

    // prepare() runs while the device is stopped, so no callback holds the
    // pointer. Retired implementations are safe to free here for the same
    // reason, and a fresh object avoids clearing the one being published.
    impl = std::make_unique<Impl>();

    if (isReverb (engine))
        impl->prepareReverb (sampleRate, engine);
    else
        impl->prepareDelay (sampleRate, engine);

    live.store (impl.get(), std::memory_order_release);
    retired.clear();
    prepared = true;
}

void AuxBus::setEngine (Engine newEngine, std::uint64_t currentBlock)
{
    if (newEngine == engine)
        return;

    engine = newEngine;

    if (! prepared)
    {
        live.store (nullptr, std::memory_order_release);
        return;
    }

    // Build the replacement completely before publishing it: process() must
    // never see cleared combs or a resized delay line.
    auto fresh = std::make_unique<Impl>();

    if (isReverb (newEngine))
        fresh->prepareReverb (sampleRate, newEngine);
    else
        fresh->prepareDelay (sampleRate, newEngine);

    live.store (fresh.get(), std::memory_order_release);
    retired.push_back ({ std::move (impl), currentBlock });
    impl = std::move (fresh);
}

void AuxBus::collectRetired (std::uint64_t currentBlock)
{
    retired.erase (std::remove_if (retired.begin(), retired.end(),
                                   [currentBlock] (const Retired& entry)
                                   {
                                       return entry.retiredAtBlock + 2 <= currentBlock;
                                   }),
                   retired.end());
}

void AuxBus::reset()
{
    if (! prepared)
        return;

    if (auto* active = live.load (std::memory_order_acquire))
        active->clearAll();
}

void AuxBus::process (juce::AudioBuffer<float>& buffer, int numSamples)
{
    auto* active = live.load (std::memory_order_acquire);

    if (! prepared || active == nullptr || buffer.getNumChannels() == 0 || numSamples <= 0)
        return;

    auto& impl = *active;
    const auto channels = juce::jmin (buffer.getNumChannels(), 2);

    if (isReverb (engine))
    {
        for (int channel = 0; channel < channels; ++channel)
        {
            auto* data = buffer.getWritePointer (channel);

            for (int i = 0; i < numSamples; ++i)
            {
                const auto output = impl.processReverbFrame (channel, data[i]);
                data[i] = std::isfinite (output) ? output : 0.0f;
            }
        }

        return;
    }

    if (engine == Engine::temporalDelay && channels > 1)
    {
        auto* left = buffer.getWritePointer (0);
        auto* right = buffer.getWritePointer (1);

        for (int i = 0; i < numSamples; ++i)
        {
            auto wetLeft = left[i];
            auto wetRight = right[i];
            impl.processTemporalDelay (wetLeft, wetRight, sampleRate);
            left[i] = std::isfinite (wetLeft) ? wetLeft : 0.0f;
            right[i] = std::isfinite (wetRight) ? wetRight : 0.0f;
        }

        return;
    }

    auto* left = buffer.getWritePointer (0);
    auto* right = channels > 1 ? buffer.getWritePointer (1) : nullptr;

    for (int i = 0; i < numSamples; ++i)
    {
        auto wetLeft = left[i];
        auto wetRight = right != nullptr ? right[i] : wetLeft;
        impl.processOverlayDelay (wetLeft, wetRight);
        left[i] = std::isfinite (wetLeft) ? wetLeft : 0.0f;

        if (right != nullptr)
            right[i] = std::isfinite (wetRight) ? wetRight : 0.0f;
    }
}
} // namespace nc
