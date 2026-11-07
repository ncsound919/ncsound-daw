#include <catch2/catch_test_macros.hpp>

#include "MixRack.h"

#include <cmath>

namespace
{
constexpr double sampleRate = 48000.0;
constexpr int blockSize = 256;
constexpr double twoPi = 6.283185307179586;

void fillSine (juce::AudioBuffer<float>& buffer, double frequency, float amplitude)
{
    for (int i = 0; i < buffer.getNumSamples(); ++i)
    {
        const auto value = amplitude * static_cast<float> (std::sin (twoPi * frequency * i / sampleRate));
        buffer.setSample (0, i, value);
        buffer.setSample (1, i, value * 0.8f);
    }
}
} // namespace

TEST_CASE ("channel strip processes and stays finite", "[mixrack]")
{
    nc::MixRackChannel channel;
    channel.prepare (sampleRate, blockSize);
    channel.setVocal (false);

    channel.setParam ("stripOn", 1.0f);
    channel.setParam ("hpfOn", 1.0f);
    channel.setParam ("hpfFreq", 80.0f);
    channel.setParam ("lowGain", 3.0f);
    channel.setParam ("lmfFreq", 700.0f);
    channel.setParam ("lmfGain", -2.0f);
    channel.setParam ("hmfFreq", 3200.0f);
    channel.setParam ("hmfGain", 2.5f);
    channel.setParam ("highGain", 1.5f);
    channel.setParam ("compOn", 1.0f);
    channel.setParam ("compThresh", -18.0f);
    channel.setParam ("compRatio", 3.0f);
    channel.setParam ("satOn", 1.0f);
    channel.setParam ("satDrive", 6.0f);
    channel.setParam ("satMix", 0.4f);
    channel.setParam ("outputDb", -1.0f);

    juce::AudioBuffer<float> buffer (2, blockSize);
    fillSine (buffer, 220.0, 0.5f);
    channel.process (buffer, blockSize);

    for (int channelIndex = 0; channelIndex < 2; ++channelIndex)
        for (int i = 0; i < blockSize; ++i)
            REQUIRE (std::isfinite (buffer.getSample (channelIndex, i)));

    REQUIRE (buffer.getMagnitude (0, blockSize) > 0.01f);
}

TEST_CASE ("strip bypass is a true passthrough", "[mixrack]")
{
    nc::MixRackChannel channel;
    channel.prepare (sampleRate, blockSize);
    channel.setParam ("stripOn", 0.0f);

    juce::AudioBuffer<float> buffer (2, blockSize);
    fillSine (buffer, 440.0, 0.25f);
    const auto before = buffer.getMagnitude (0, blockSize);
    channel.process (buffer, blockSize);

    REQUIRE (buffer.getMagnitude (0, blockSize) == before);
}

TEST_CASE ("open mic only colours vocal tracks", "[mixrack]")
{
    juce::AudioBuffer<float> dry (2, blockSize);
    fillSine (dry, 180.0, 0.4f);

    nc::MixRackChannel nonVocal;
    nonVocal.prepare (sampleRate, blockSize);
    nonVocal.setParam ("openMicOn", 1.0f);
    nonVocal.setParam ("openMicMix", 0.8f);
    nonVocal.setParam ("stripOn", 0.0f);

    auto nonVocalBuffer = dry;
    nonVocal.process (nonVocalBuffer, blockSize);
    REQUIRE (nonVocalBuffer.getMagnitude (0, blockSize) == dry.getMagnitude (0, blockSize));

    nc::MixRackChannel vocal;
    vocal.prepare (sampleRate, blockSize);
    vocal.setVocal (true);
    vocal.setParam ("openMicOn", 1.0f);
    vocal.setParam ("openMicMix", 0.8f);
    vocal.setParam ("openMicDrive", 9.0f);
    vocal.setParam ("stripOn", 1.0f);
    vocal.setParam ("eqOn", 0.0f);

    auto vocalBuffer = dry;
    vocal.process (vocalBuffer, blockSize);

    REQUIRE (vocalBuffer.getMagnitude (0, blockSize) > 0.0f);

    bool differs = false;

    for (int i = 0; i < blockSize; ++i)
        differs = differs || std::abs (vocalBuffer.getSample (0, i) - dry.getSample (0, i)) > 1.0e-5f;

    REQUIRE (differs);
}

TEST_CASE ("open mic is baked into every vocal track", "[mixrack]")
{
    juce::AudioBuffer<float> dry (2, blockSize);
    fillSine (dry, 180.0, 0.4f);

    nc::MixRackChannel channel;
    channel.prepare (sampleRate, blockSize);
    channel.setParam ("eqOn", 0.0f);
    channel.setVocal (true); // the role alone, with no explicit stage settings

    REQUIRE (channel.params.openMicOn.load());

    auto buffer = dry;
    channel.process (buffer, blockSize);

    bool differs = false;

    for (int i = 0; i < blockSize; ++i)
        differs = differs || std::abs (buffer.getSample (0, i) - dry.getSample (0, i)) > 1.0e-5f;

    REQUIRE (differs);

    nc::MixRackChannel nonVocal;
    nonVocal.prepare (sampleRate, blockSize);
    nonVocal.setVocal (false);
    REQUIRE_FALSE (nonVocal.params.openMicOn.load());
}

TEST_CASE ("an explicit mic bypass outlives the vocal role", "[mixrack]")
{
    juce::AudioBuffer<float> dry (2, blockSize);
    fillSine (dry, 180.0, 0.4f);

    nc::MixRackChannel channel;
    channel.prepare (sampleRate, blockSize);
    channel.setParam ("eqOn", 0.0f);
    channel.setParam ("openMicOn", 0.0f);
    channel.setVocal (true);

    REQUIRE_FALSE (channel.params.openMicOn.load());

    auto buffer = dry;
    channel.process (buffer, blockSize);

    for (int i = 0; i < blockSize; ++i)
        REQUIRE (buffer.getSample (0, i) == dry.getSample (0, i));
}

TEST_CASE ("a saved mic override survives a project round trip", "[mixrack]")
{
    nc::MixRackChannel saved;
    saved.setParam ("openMicOn", 0.0f);
    saved.setVocal (true);

    nc::MixRackChannel restored;
    restored.fromVar (saved.toVar());
    restored.setVocal (true);
    REQUIRE_FALSE (restored.params.openMicOn.load());

    // A session written before the stage was baked in has no override flag, so
    // its vocal tracks adopt the new default on the next load.
    nc::MixRackChannel migrated;
    migrated.fromVar (juce::JSON::parse ("{\"vocal\":true,\"openMicOn\":false}"));
    migrated.setVocal (true);
    REQUIRE (migrated.params.openMicOn.load());
}

TEST_CASE ("vocal track names resolve to the vocal role", "[mixrack]")
{
    REQUIRE (nc::isVocalTrackName ("VOX"));
    REQUIRE (nc::isVocalTrackName ("Lead Vocal"));
    REQUIRE (nc::isVocalTrackName ("BGV 1"));
    REQUIRE (nc::isVocalTrackName ("ADLIB"));
    REQUIRE_FALSE (nc::isVocalTrackName ("BASS"));
    REQUIRE_FALSE (nc::isVocalTrackName ("FX 2"));
}

TEST_CASE ("channel low-pass shapes the top end", "[mixrack]")
{
    nc::MixRackChannel channel;
    channel.prepare (sampleRate, blockSize);
    channel.setParam ("hpfOn", 0.0f);
    channel.setParam ("lpfOn", 1.0f);
    channel.setParam ("lpfFreq", 2000.0f);

    juce::AudioBuffer<float> buffer (2, blockSize);
    fillSine (buffer, 10000.0, 0.5f);
    channel.process (buffer, blockSize);

    REQUIRE (buffer.getMagnitude (0, blockSize) < 0.1f);
}

TEST_CASE ("master rack processes and A/B recalls snapshots", "[mixrack]")
{
    nc::MasterRack master;
    master.prepare (sampleRate, blockSize);

    master.setParam ("lowGain", 3.0f);
    master.setParam ("lpfOn", 1.0f);
    master.setParam ("lpfFreq", 16000.0f);
    master.setParam ("midGain", -2.0f);
    master.setParam ("highGain", 1.0f);
    master.setParam ("dynOn", 1.0f);
    master.setParam ("imgOn", 1.0f);
    master.setParam ("width", 1.2f);
    master.setParam ("automixOn", 1.0f);
    master.setParam ("targetLufs", -16.0f);

    master.captureAB (0);

    master.setParam ("lowGain", -4.0f);
    master.setParam ("lpfFreq", 9000.0f);
    master.setParam ("width", 0.7f);
    master.setParam ("automixOn", 0.0f);
    master.captureAB (1);

    master.recallAB (0);
    REQUIRE (master.getParam ("lowGain") == 3.0f);
    REQUIRE (master.getParam ("lpfOn") == 1.0f);
    REQUIRE (master.getParam ("lpfFreq") == 16000.0f);
    REQUIRE (master.getParam ("width") == 1.2f);
    REQUIRE (master.getParam ("automixOn") == 1.0f);

    master.recallAB (1);
    REQUIRE (master.getParam ("lowGain") == -4.0f);
    REQUIRE (master.getParam ("lpfFreq") == 9000.0f);
    REQUIRE (master.getParam ("width") == 0.7f);
    REQUIRE (master.getParam ("automixOn") == 0.0f);

    // 100 blocks is 0.53 s: the 3 s short-term meter needs more than its
    // 400 ms sub-block to leave silence.
    for (int block = 0; block < 100; ++block)
    {
        juce::AudioBuffer<float> buffer (2, blockSize);
        fillSine (buffer, 1000.0, 0.6f);
        master.process (buffer, blockSize);

        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < blockSize; ++i)
                REQUIRE (std::isfinite (buffer.getSample (c, i)));

        REQUIRE (buffer.getMagnitude (0, blockSize) > 0.0f);
    }

    REQUIRE (master.shortTermLufs() > -70.0f);
    REQUIRE (master.latencySamples() >= 0);
}

TEST_CASE ("master comparison bypass leaves the signal alone apart from trim", "[mixrack]")
{
    nc::MasterRack master;
    master.prepare (sampleRate, blockSize);
    master.setParam ("compare", 1.0f);
    master.setParam ("trimDb", 0.0f);
    master.setParam ("maxOn", 1.0f);

    juce::AudioBuffer<float> buffer (2, blockSize);
    fillSine (buffer, 500.0, 0.3f);
    const auto impulse = buffer.getSample (0, 40);
    master.process (buffer, blockSize);

    REQUIRE (buffer.getSample (0, 40) == impulse);
}
