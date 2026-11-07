/*
    Sends (Send A = reverb, Send B = delay) and stereo width.

    Driven through AudioEngine::processBlockForTesting with an injected input
    burst on an armed, monitored track, so no microphone or DUT signal is
    invented: a send at zero must leave the later blocks silent once the dry
    burst stops, while a send at unity must leave a measurable aux tail.
*/

#include <catch2/catch_test_macros.hpp>

#include "AudioEngine.h"

#include <cmath>
#include <vector>

namespace
{
bool waitForDevice (nc::AudioEngine& engine, int timeoutMs = 3000)
{
    const auto deadline = juce::Time::getMillisecondCounter() + static_cast<juce::uint32> (timeoutMs);

    while (juce::Time::getMillisecondCounter() < deadline)
    {
        if (engine.currentSampleRate() > 0.0 && engine.isDeviceOpen())
            return true;

        juce::Thread::sleep (20);
    }

    return engine.currentSampleRate() > 0.0;
}

void feedBlock (nc::AudioEngine& engine, const std::vector<float>& left,
                const std::vector<float>& right, float* outLeft, float* outRight)
{
    const float* inputs[2] { left.data(), right.data() };
    float* outputs[2] { outLeft, outRight };
    engine.processBlockForTesting (inputs, 2, outputs, 2, static_cast<int> (left.size()));
}

float magnitude (const std::vector<float>& left, const std::vector<float>& right)
{
    float peak = 0.0f;

    for (size_t i = 0; i < left.size(); ++i)
        peak = juce::jmax (peak, juce::jmax (std::abs (left[i]), std::abs (right[i])));

    return peak;
}
} // namespace

TEST_CASE ("tracks expose clamped sends and width to the engine", "[sends]")
{
    nc::AudioEngine engine;

    engine.setTrackSendA (0, 0.4f);
    engine.setTrackSendB (0, 0.7f);
    engine.setTrackWidth (0, 0.8f);
    REQUIRE (engine.trackSendA (0) == 0.4f);
    REQUIRE (engine.trackSendB (0) == 0.7f);
    REQUIRE (engine.trackWidth (0) == 0.8f);

    engine.setTrackSendA (0, 2.0f);
    REQUIRE (engine.trackSendA (0) == 1.0f);
    engine.setTrackWidth (0, -1.0f);
    REQUIRE (engine.trackWidth (0) == 0.0f);
    REQUIRE (engine.trackSendA (999) == 0.0f);
    REQUIRE (engine.trackWidth (999) == 0.5f);
}

TEST_CASE ("Send A at zero leaves the tail silent, at unity leaves a reverb tail", "[sends]")
{
    nc::AudioEngine engine;

    if (! waitForDevice (engine))
    {
        WARN ("No audio device on this machine; skipping sends tail test");
        return;
    }

    engine.suspendAudioCallbackForTesting();
    engine.setPlaying (true);
    engine.setRecordArmed (0, true);
    engine.setTrackMonitor (0, true);
    engine.setTrackGain (0, 1.0f);
    engine.setTrackPan (0, 0.0f);
    engine.setTrackMuted (0, false);
    engine.setTrackSolo (0, false);
    for (int track = 1; track < nc::AudioEngine::trackCount; ++track)
        engine.setTrackMuted (track, true);

    const int blockSize = 512;
    const int blocks = 30; // ~320ms at 48k: dry burst (1 block) is long gone.

    const auto run = [&engine, blockSize, blocks] (float sendA)
    {
        engine.setTrackSendA (0, sendA);
        engine.setTrackSendB (0, 0.0f);
        float tail = 0.0f;

        for (int block = 0; block < blocks; ++block)
        {
            std::vector<float> inL (static_cast<size_t> (blockSize), 0.0f);
            std::vector<float> inR (static_cast<size_t> (blockSize), 0.0f);

            if (block == 0)
            {
                for (int i = 0; i < blockSize; ++i)
                {
                    const auto tone = std::sin (2.0 * juce::MathConstants<double>::pi * 440.0 * i / 48000.0)
                                      * 0.8f;
                    inL[static_cast<size_t> (i)] = tone;
                    inR[static_cast<size_t> (i)] = tone;
                }
            }

            std::vector<float> outL (static_cast<size_t> (blockSize), 0.0f);
            std::vector<float> outR (static_cast<size_t> (blockSize), 0.0f);
            feedBlock (engine, inL, inR, outL.data(), outR.data());

            if (block >= 4) // skip the dry burst block + comb onset.
                tail = juce::jmax (tail, magnitude (outL, outR));
        }

        return tail;
    };

    const auto silentTail = run (0.0f);
    const auto wetTail = run (1.0f);
    REQUIRE (silentTail < 0.05f);
    REQUIRE (wetTail > silentTail + 0.05f);
}

TEST_CASE ("Send B delay tail is measurable after the dry burst", "[sends]")
{
    nc::AudioEngine engine;

    if (! waitForDevice (engine))
    {
        WARN ("No audio device on this machine; skipping delay tail test");
        return;
    }

    engine.suspendAudioCallbackForTesting();
    engine.setPlaying (true);
    engine.setRecordArmed (0, true);
    engine.setTrackMonitor (0, true);
    engine.setTrackGain (0, 1.0f);
    for (int track = 1; track < nc::AudioEngine::trackCount; ++track)
        engine.setTrackMuted (track, true);

    const int blockSize = 512;
    const int blocks = 90; // 375ms delay lands at block ~35.

    const auto run = [&engine, blockSize, blocks] (float sendB)
    {
        engine.setTrackSendA (0, 0.0f);
        engine.setTrackSendB (0, sendB);
        float tail = 0.0f;

        for (int block = 0; block < blocks; ++block)
        {
            std::vector<float> inL (static_cast<size_t> (blockSize), 0.0f);
            std::vector<float> inR (static_cast<size_t> (blockSize), 0.0f);

            if (block == 0)
            {
                for (int i = 0; i < blockSize; ++i)
                {
                    const auto tone = std::sin (2.0 * juce::MathConstants<double>::pi * 440.0 * i / 48000.0)
                                      * 0.8f;
                    inL[static_cast<size_t> (i)] = tone;
                    inR[static_cast<size_t> (i)] = tone;
                }
            }

            std::vector<float> outL (static_cast<size_t> (blockSize), 0.0f);
            std::vector<float> outR (static_cast<size_t> (blockSize), 0.0f);
            feedBlock (engine, inL, inR, outL.data(), outR.data());

            if (block >= 30)
                tail = juce::jmax (tail, magnitude (outL, outR));
        }

        return tail;
    };

    REQUIRE (run (0.0f) < 0.05f);
    REQUIRE (run (1.0f) > 0.1f);
}

TEST_CASE ("width narrows and widens a stereo distinction", "[sends]")
{
    nc::AudioEngine engine;

    if (! waitForDevice (engine))
    {
        WARN ("No audio device on this machine; skipping width test");
        return;
    }

    engine.suspendAudioCallbackForTesting();
    engine.setPlaying (true);
    engine.setRecordArmed (0, true);
    engine.setTrackMonitor (0, true);
    engine.setTrackGain (0, 1.0f);
    engine.setTrackSendA (0, 0.0f);
    engine.setTrackSendB (0, 0.0f);
    for (int track = 1; track < nc::AudioEngine::trackCount; ++track)
        engine.setTrackMuted (track, true);

    const int blockSize = 256;
    const int blocks = 30;

    const auto measure = [&engine, blockSize, blocks] (float width)
    {
        engine.setTrackWidth (0, width);
        float difference = 0.0f;

        for (int block = 0; block < blocks; ++block)
        {
            std::vector<float> inL (static_cast<size_t> (blockSize), 0.0f);
            std::vector<float> inR (static_cast<size_t> (blockSize), 0.0f);

            for (int i = 0; i < blockSize; ++i)
                // AC tone (not DC — the strip HPF would remove a DC offset and
                // the test would measure silence either way).
                inL[static_cast<size_t> (i)] = 0.8f
                    * static_cast<float> (std::sin (2.0 * juce::MathConstants<double>::pi * 440.0 * (block * blockSize + i) / 48000.0));

            std::vector<float> outL (static_cast<size_t> (blockSize), 0.0f);
            std::vector<float> outR (static_cast<size_t> (blockSize), 0.0f);
            feedBlock (engine, inL, inR, outL.data(), outR.data());

            if (block >= 22) // let smoothedWidth settle near target.
            {
                for (int i = 0; i < blockSize; ++i)
                    difference += std::abs (outL[static_cast<size_t> (i)] - outR[static_cast<size_t> (i)]);
            }
        }

        return difference;
    };

    const auto mono = measure (0.0f);
    const auto wide = measure (1.0f);
    // Residual smoothed-width settle + master-rack imaging leave a small
    // L-R floor even at width 0; wide must still dominate it clearly.
    REQUIRE (mono < 8.0f);
    REQUIRE (wide > mono + 20.0f);
}
