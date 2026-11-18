#include <catch2/catch_test_macros.hpp>

#include "MixRack.h"
#include "MixRackDsp.h"

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

TEST_CASE ("pro EQ shapes, slopes and filters reach the audio", "[mixrack][eq]")
{
    nc::MixRackChannel channel;
    channel.prepare (sampleRate, blockSize);
    channel.setParam ("hpfOn", 0.0f);
    channel.setParam ("lpfOn", 1.0f);
    channel.setParam ("lpfFreq", 1500.0f);
    channel.setParam ("lpfSlope", 3.0f);

    juce::AudioBuffer<float> buffer (2, blockSize);
    fillSine (buffer, 9000.0, 0.5f);
    channel.process (buffer, blockSize);
    const auto steep = buffer.getMagnitude (0, blockSize);

    REQUIRE (steep < 0.05f);

    // The same corner with the shallowest slope must let more through.
    nc::MixRackChannel shallow;
    shallow.prepare (sampleRate, blockSize);
    shallow.setParam ("hpfOn", 0.0f);
    shallow.setParam ("lpfOn", 1.0f);
    shallow.setParam ("lpfFreq", 1500.0f);
    shallow.setParam ("lpfSlope", 0.0f);

    juce::AudioBuffer<float> shallowBuffer (2, blockSize);
    fillSine (shallowBuffer, 9000.0, 0.5f);
    shallow.process (shallowBuffer, blockSize);

    REQUIRE (shallowBuffer.getMagnitude (0, blockSize) > steep * 1.2f);

    // A +12 dB low shelf at 80 Hz must lift a 60 Hz tone well above unity.
    nc::MixRackChannel shelf;
    shelf.prepare (sampleRate, blockSize);
    shelf.setParam ("hpfOn", 0.0f);
    shelf.setParam ("lpfOn", 0.0f);
    shelf.setParam ("band1On", 1.0f);
    shelf.setParam ("band1Type", 1.0f);
    shelf.setParam ("band1Freq", 80.0f);
    shelf.setParam ("band1Gain", 12.0f);

    juce::AudioBuffer<float> tone (2, blockSize);
    fillSine (tone, 60.0, 0.2f);
    shelf.process (tone, blockSize);

    REQUIRE (tone.getMagnitude (0, blockSize) > 0.3f);
}

TEST_CASE ("a single dynamic band reaches its reference gain", "[mixrack][eq][dsp]")
{
    nc::mixrack::EqBandV2 band;
    band.prepare (sampleRate, 4);

    nc::mixrack::EqBandSettings s;
    s.on = true;
    s.type = 0;
    s.freq = 250.0f;
    s.gainDb = -6.0f;
    s.q = 2.0f;
    s.dynOn = true;
    s.dynThreshDb = -35.0f;
    s.dynRangeDb = -12.0f;
    band.setEnabled (true);
    band.configure (s);

    float l = 0.0f, r = 0.0f;
    for (int i = 0; i < blockSize; ++i)
    {
        l = 0.5f * (float) std::sin (twoPi * 250.0 * i / sampleRate);
        r = l;
        band.process (l, r);
    }

    INFO ("single band output: " << l << ", scale: " << band.debugScale()
                                 << ", dyn: " << band.dynamicGainDb()
                                 << ", ref: " << band.debugRefGainDb());
    REQUIRE (std::abs (l) < 0.2f);
}

TEST_CASE ("dynamic EQ only moves the band while it is loud", "[mixrack][eq]")
{
    nc::MixRackChannel channel;
    channel.prepare (sampleRate, blockSize);
    channel.setParam ("hpfOn", 0.0f);
    channel.setParam ("lpfOn", 0.0f);
    channel.setParam ("band1On", 1.0f);
    channel.setParam ("band1Type", 0.0f);
    channel.setParam ("band1Freq", 250.0f);
    channel.setParam ("band1Gain", -6.0f);
    channel.setParam ("band1Q", 2.0f);
    channel.setParam ("band1Dyn", 1.0f);
    channel.setParam ("band1DynThresh", -35.0f);
    channel.setParam ("band1DynRange", -12.0f); // negative range cuts while loud

    REQUIRE (channel.getParam ("band1Dyn") == 1.0f);
    REQUIRE (channel.getParam ("band1DynRange") == -12.0f);
    REQUIRE (channel.getParam ("band1Gain") == -6.0f);

    juce::AudioBuffer<float> quiet (2, blockSize);
    fillSine (quiet, 250.0, 0.01f);
    channel.process (quiet, blockSize);
    const auto quietOut = quiet.getMagnitude (0, blockSize);
    REQUIRE (quietOut > 0.005f);

    // One continuous sine: restarting the phase every block would inject a
    // 5 ms discontinuity into a high-Q dynamic band and hide the steady state.
    juce::AudioBuffer<float> loud (2, blockSize * 20);
    for (int i = 0; i < loud.getNumSamples(); ++i)
    {
        const auto value = 0.5f * (float) std::sin (twoPi * 250.0 * i / sampleRate);
        loud.setSample (0, i, value);
        loud.setSample (1, i, value * 0.8f);
    }

    channel.process (loud, loud.getNumSamples());
    const auto loudOut = loud.getMagnitude (0, loud.getNumSamples() - blockSize, blockSize);

    // Compare gains, not absolute levels: the loud test tone is 50x larger.
    const auto quietGain = quietOut / 0.01f;
    const auto loudGain = loudOut / 0.5f;

    INFO ("quiet gain: " << quietGain << ", loud gain: " << loudGain
                         << ", dyn: " << channel.bandDynamicGainDb (0) << " dB");
    REQUIRE (loudGain < quietGain * 0.8f);
    REQUIRE (loudGain > 0.005f);
}

TEST_CASE ("mid and side bands only touch their own signal", "[mixrack][eq]")
{
    nc::MixRackChannel channel;
    channel.prepare (sampleRate, blockSize);
    channel.setParam ("hpfOn", 0.0f);
    channel.setParam ("lpfOn", 0.0f);
    channel.setParam ("band1On", 1.0f);
    channel.setParam ("band1Type", 0.0f);
    channel.setParam ("band1Freq", 1000.0f);
    channel.setParam ("band1Gain", 12.0f);
    channel.setParam ("band1Q", 1.0f);
    channel.setParam ("band1Channel", 2.0f); // side only

    juce::AudioBuffer<float> side (2, blockSize);
    fillSine (side, 1000.0, 0.3f);
    side.setSample (1, 0, -side.getSample (0, 0));
    for (int i = 0; i < blockSize; ++i)
        side.setSample (1, i, -side.getSample (0, i));

    const auto sideIn = side.getMagnitude (0, blockSize);
    channel.process (side, blockSize);
    REQUIRE (side.getMagnitude (0, blockSize) > sideIn * 1.2f);

    nc::MixRackChannel midChannel;
    midChannel.prepare (sampleRate, blockSize);
    midChannel.setParam ("hpfOn", 0.0f);
    midChannel.setParam ("lpfOn", 0.0f);
    midChannel.setParam ("band1On", 1.0f);
    midChannel.setParam ("band1Type", 0.0f);
    midChannel.setParam ("band1Freq", 1000.0f);
    midChannel.setParam ("band1Gain", 12.0f);
    midChannel.setParam ("band1Q", 1.0f);
    midChannel.setParam ("band1Channel", 2.0f);

    juce::AudioBuffer<float> mid (2, blockSize);
    fillSine (mid, 1000.0, 0.3f);
    for (int i = 0; i < blockSize; ++i)
        mid.setSample (1, i, mid.getSample (0, i));

    const auto midIn = mid.getMagnitude (0, blockSize);
    midChannel.process (mid, blockSize);
    REQUIRE (std::abs (mid.getMagnitude (0, blockSize) - midIn) < 1.0e-4f);
}

TEST_CASE ("every saturation character is finite and audibly different", "[mixrack][sat]")
{
    juce::AudioBuffer<float> reference (2, blockSize);
    fillSine (reference, 220.0, 0.4f);

    float previousMagnitude = -1.0f;

    for (int mode = 0; mode < 8; ++mode)
    {
        nc::MixRackChannel channel;
        channel.prepare (sampleRate, blockSize);
        channel.setParam ("hpfOn", 0.0f);
        channel.setParam ("lpfOn", 0.0f);
        channel.setParam ("eqOn", 0.0f);
        channel.setParam ("satOn", 1.0f);
        channel.setParam ("satMode", (float) mode);
        channel.setParam ("satDrive", 12.0f);
        channel.setParam ("satMix", 1.0f);
        channel.setParam ("satBias", 0.1f);
        channel.setParam ("satTone", 0.3f);
        channel.setParam ("satDynamics", 0.5f);
        channel.setParam ("satOversample", 1.0f);

        auto buffer = reference;
        channel.process (buffer, blockSize);

        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < blockSize; ++i)
                REQUIRE (std::isfinite (buffer.getSample (c, i)));

        const auto magnitude = buffer.getMagnitude (0, blockSize);
        REQUIRE (magnitude > 0.02f);
        REQUIRE (magnitude < 2.0f);

        bool differs = false;
        for (int i = 0; i < blockSize; ++i)
            differs = differs || std::abs (buffer.getSample (0, i) - reference.getSample (0, i)) > 1.0e-5f;

        REQUIRE (differs);
        REQUIRE (magnitude != previousMagnitude);
        previousMagnitude = magnitude;
    }
}

TEST_CASE ("compressor characters all reduce a hot signal", "[mixrack][comp]")
{
    juce::AudioBuffer<float> reference (2, blockSize);
    fillSine (reference, 220.0, 0.8f);
    const auto inputMagnitude = reference.getMagnitude (0, blockSize);

    for (int type = 0; type < 8; ++type)
    {
        nc::MixRackChannel channel;
        channel.prepare (sampleRate, blockSize);
        channel.setParam ("hpfOn", 0.0f);
        channel.setParam ("lpfOn", 0.0f);
        channel.setParam ("eqOn", 0.0f);
        channel.setParam ("compOn", 1.0f);
        channel.setParam ("compType", (float) type);
        channel.setParam ("compThresh", -30.0f);
        channel.setParam ("compRatio", 8.0f);
        channel.setParam ("compAttack", 1.0f);
        channel.setParam ("compRelease", 100.0f);
        channel.setParam ("compMakeup", 0.0f);
        channel.setParam ("compMix", 1.0f);

        auto buffer = reference;
        channel.process (buffer, blockSize);

        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < blockSize; ++i)
                REQUIRE (std::isfinite (buffer.getSample (c, i)));

        REQUIRE (buffer.getMagnitude (0, blockSize) <= inputMagnitude + 1.0e-5f);
        REQUIRE (buffer.getMagnitude (0, blockSize) > 0.01f);
    }
}

TEST_CASE ("master exposes eight EQ bands, typed dynamics and saturation", "[mixrack][master]")
{
    nc::MasterRack master;
    master.prepare (sampleRate, blockSize);

    master.setParam ("eqOn", 1.0f);
    master.setParam ("hpfOn", 0.0f);
    master.setParam ("lpfOn", 0.0f);
    master.setParam ("band6On", 1.0f);
    master.setParam ("band6Type", 0.0f);
    master.setParam ("band6Freq", 8000.0f);
    master.setParam ("band6Gain", -18.0f);

    juce::AudioBuffer<float> top (2, blockSize * 20);
    for (int i = 0; i < top.getNumSamples(); ++i)
    {
        const auto value = 0.4f * (float) std::sin (twoPi * 8000.0 * i / sampleRate);
        top.setSample (0, i, value);
        top.setSample (1, i, value);
    }
    master.process (top, top.getNumSamples());
    REQUIRE (top.getMagnitude (0, top.getNumSamples() - blockSize, blockSize) < 0.15f);

    master.setParam ("satOn", 1.0f);
    master.setParam ("satMode", 5.0f); // fold
    master.setParam ("satDrive", 8.0f);
    master.setParam ("satMix", 1.0f);
    master.setParam ("dynOn", 1.0f);
    master.setParam ("dynType", 1.0f);
    master.setParam ("dynBands", 3.0f);
    master.setParam ("dyn1Thresh", -24.0f);
    master.setParam ("dyn1Ratio", 6.0f);
    master.setParam ("maxOn", 1.0f);
    master.setParam ("ceiling", -1.0f);

    for (int block = 0; block < 20; ++block)
    {
        juce::AudioBuffer<float> buffer (2, blockSize);
        fillSine (buffer, 300.0, 0.7f);
        master.process (buffer, blockSize);

        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < blockSize; ++i)
                REQUIRE (std::isfinite (buffer.getSample (c, i)));
    }
}

TEST_CASE ("new strip parameters survive a state round trip", "[mixrack][state]")
{
    nc::MixRackChannel saved;
    saved.setParam ("band5On", 1.0f);
    saved.setParam ("band5Type", 5.0f);
    saved.setParam ("band5Freq", 420.0f);
    saved.setParam ("band5Gain", -4.5f);
    saved.setParam ("band5Q", 3.5f);
    saved.setParam ("band5Slope", 2.0f);
    saved.setParam ("band5Channel", 1.0f);
    saved.setParam ("band5Dyn", 1.0f);
    saved.setParam ("band5DynThresh", -33.0f);
    saved.setParam ("band5DynRange", -6.0f);
    saved.setParam ("compType", 5.0f);
    saved.setParam ("compDrive", 6.0f);
    saved.setParam ("compOutput", -3.0f);
    saved.setParam ("compColor", 0.8f);
    saved.setParam ("compFeedback", 0.2f);
    saved.setParam ("satMode", 6.0f);
    saved.setParam ("satDynamics", 0.7f);
    saved.setParam ("satFeedback", 0.2f);
    saved.setParam ("satTonePresence", 0.4f);
    saved.setParam ("inputTrim", -3.0f);
    saved.setParam ("phase", 1.0f);
    saved.setParam ("hpfSlope", 2.0f);

    nc::MixRackChannel restored;
    restored.fromVar (saved.toVar());

    REQUIRE (restored.getParam ("band5On") == 1.0f);
    REQUIRE (restored.getParam ("band5Type") == 5.0f);
    REQUIRE (restored.getParam ("band5Freq") == 420.0f);
    REQUIRE (restored.getParam ("band5Gain") == -4.5f);
    REQUIRE (restored.getParam ("band5Q") == 3.5f);
    REQUIRE (restored.getParam ("band5Slope") == 2.0f);
    REQUIRE (restored.getParam ("band5Channel") == 1.0f);
    REQUIRE (restored.getParam ("band5Dyn") == 1.0f);
    REQUIRE (restored.getParam ("band5DynRange") == -6.0f);
    REQUIRE (restored.getParam ("compType") == 5.0f);
    REQUIRE (restored.getParam ("compDrive") == 6.0f);
    REQUIRE (restored.getParam ("compOutput") == -3.0f);
    REQUIRE (restored.getParam ("compColor") == 0.8f);
    REQUIRE (restored.getParam ("compFeedback") == 0.2f);
    REQUIRE (restored.getParam ("satMode") == 6.0f);
    REQUIRE (restored.getParam ("satDynamics") == 0.7f);
    REQUIRE (restored.getParam ("satFeedback") == 0.2f);
    REQUIRE (restored.getParam ("satTonePresence") == 0.4f);
    REQUIRE (restored.getParam ("inputTrim") == -3.0f);
    REQUIRE (restored.getParam ("phase") == 1.0f);
    REQUIRE (restored.getParam ("hpfSlope") == 2.0f);

    nc::MasterRack master;
    master.setParam ("band8On", 1.0f);
    master.setParam ("band8Type", 4.0f);
    master.setParam ("band8Freq", 18000.0f);
    master.setParam ("dynType", 5.0f);
    master.setParam ("satMode", 3.0f);
    master.setParam ("maxMode", 2.0f);
    master.setParam ("deqOn", 1.0f);
    master.setParam ("deq3On", 1.0f);
    master.setParam ("deq3Freq", 3200.0f);
    master.setParam ("deq3Thresh", -28.0f);
    master.setParam ("deq3Range", -5.0f);
    master.setParam ("exc1Mode", 3.0f);
    master.setParam ("exc1Dyn", 0.4f);

    nc::MasterRack restoredMaster;
    restoredMaster.fromVar (master.toVar());

    REQUIRE (restoredMaster.getParam ("band8On") == 1.0f);
    REQUIRE (restoredMaster.getParam ("band8Freq") == 18000.0f);
    REQUIRE (restoredMaster.getParam ("dynType") == 5.0f);
    REQUIRE (restoredMaster.getParam ("satMode") == 3.0f);
    REQUIRE (restoredMaster.getParam ("maxMode") == 2.0f);
    REQUIRE (restoredMaster.getParam ("deqOn") == 1.0f);
    REQUIRE (restoredMaster.getParam ("deq3On") == 1.0f);
    REQUIRE (restoredMaster.getParam ("deq3Freq") == 3200.0f);
    REQUIRE (restoredMaster.getParam ("deq3Thresh") == -28.0f);
    REQUIRE (restoredMaster.getParam ("deq3Range") == -5.0f);
    REQUIRE (restoredMaster.getParam ("exc1Mode") == 3.0f);
    REQUIRE (restoredMaster.getParam ("exc1Dyn") == 0.4f);
}

TEST_CASE ("master dynamic EQ cuts only above its threshold", "[mixrack][deq]")
{
    nc::MasterRack master;
    master.prepare (sampleRate, blockSize);
    master.setParam ("eqOn", 0.0f);
    master.setParam ("maxOn", 0.0f);
    master.setParam ("deqOn", 1.0f);
    master.setParam ("deq3On", 1.0f);
    master.setParam ("deq3Type", 0.0f);
    master.setParam ("deq3Freq", 1000.0f);
    master.setParam ("deq3Q", 2.0f);
    master.setParam ("deq3Thresh", -30.0f);
    master.setParam ("deq3Range", -9.0f);
    master.setParam ("deq3Atk", 5.0f);
    master.setParam ("deq3Rel", 80.0f);

    juce::AudioBuffer<float> quiet (2, blockSize * 20);
    for (int i = 0; i < quiet.getNumSamples(); ++i)
    {
        const auto value = 0.01f * (float) std::sin (twoPi * 1000.0 * i / sampleRate);
        quiet.setSample (0, i, value);
        quiet.setSample (1, i, value);
    }
    master.process (quiet, quiet.getNumSamples());
    const auto quietGain = quiet.getMagnitude (0, quiet.getNumSamples() - blockSize, blockSize) / 0.01f;

    juce::AudioBuffer<float> loud (2, blockSize * 20);
    for (int i = 0; i < loud.getNumSamples(); ++i)
    {
        const auto value = 0.5f * (float) std::sin (twoPi * 1000.0 * i / sampleRate);
        loud.setSample (0, i, value);
        loud.setSample (1, i, value);
    }
    master.process (loud, loud.getNumSamples());
    const auto loudGain = loud.getMagnitude (0, loud.getNumSamples() - blockSize, blockSize) / 0.5f;

    REQUIRE (quietGain > 0.8f);
    REQUIRE (loudGain < quietGain * 0.6f);
}

TEST_CASE ("saturation tone, dynamics and feedback stay finite", "[mixrack][sat]")
{
    for (int mode = 0; mode < 8; ++mode)
    {
        for (const auto dynamics : { -0.6f, 0.6f })
        {
            nc::MixRackChannel channel;
            channel.prepare (sampleRate, blockSize);
            channel.setParam ("hpfOn", 0.0f);
            channel.setParam ("lpfOn", 0.0f);
            channel.setParam ("eqOn", 0.0f);
            channel.setParam ("satOn", 1.0f);
            channel.setParam ("satMode", (float) mode);
            channel.setParam ("satDrive", 10.0f);
            channel.setParam ("satMix", 1.0f);
            channel.setParam ("satDynamics", dynamics);
            channel.setParam ("satFeedback", 0.25f);
            channel.setParam ("satFeedbackHz", 180.0f);
            channel.setParam ("satToneBass", 0.4f);
            channel.setParam ("satToneMid", -0.3f);
            channel.setParam ("satToneTreble", 0.5f);
            channel.setParam ("satTonePresence", 0.2f);
            channel.setParam ("satOversample", 1.0f);

            juce::AudioBuffer<float> buffer (2, blockSize * 8);
            for (int i = 0; i < buffer.getNumSamples(); ++i)
            {
                const auto value = 0.4f * (float) std::sin (twoPi * 220.0 * i / sampleRate);
                buffer.setSample (0, i, value);
                buffer.setSample (1, i, value * 0.9f);
            }

            channel.process (buffer, buffer.getNumSamples());

            for (int c = 0; c < 2; ++c)
                for (int i = 0; i < buffer.getNumSamples(); ++i)
                    REQUIRE (std::isfinite (buffer.getSample (c, i)));

            REQUIRE (buffer.getMagnitude (0, buffer.getNumSamples()) < 2.0f);
        }
    }
}

TEST_CASE ("input trim and polarity are applied before the strip", "[mixrack]")
{
    nc::MixRackChannel channel;
    channel.prepare (sampleRate, blockSize);
    channel.setParam ("eqOn", 0.0f);
    channel.setParam ("compOn", 0.0f);
    channel.setParam ("satOn", 0.0f);
    channel.setParam ("inputTrim", -6.0f);
    channel.setParam ("phase", 1.0f);

    juce::AudioBuffer<float> buffer (2, blockSize);
    fillSine (buffer, 440.0, 0.5f);
    const auto firstSample = buffer.getSample (0, 10);
    channel.process (buffer, blockSize);

    REQUIRE (std::abs (buffer.getSample (0, 10) - (-firstSample * 0.501187f)) < 1.0e-5f);
}

TEST_CASE ("compressor colour, drive and feedback are finite and type-specific", "[mixrack][comp]")
{
    auto makeBuffer = []
    {
        juce::AudioBuffer<float> buffer (2, blockSize * 12);
        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            const auto value = 0.45f * (float) std::sin (twoPi * 220.0 * i / sampleRate);
            buffer.setSample (0, i, value);
            buffer.setSample (1, i, value * 0.9f);
        }
        return buffer;
    };

    std::array<float, 8> magnitudes {};

    for (int type = 0; type < 8; ++type)
    {
        nc::MixRackChannel channel;
        channel.prepare (sampleRate, blockSize);
        channel.setParam ("hpfOn", 0.0f);
        channel.setParam ("lpfOn", 0.0f);
        channel.setParam ("eqOn", 0.0f);
        channel.setParam ("compOn", 1.0f);
        channel.setParam ("compType", (float) type);
        channel.setParam ("compThresh", -30.0f);
        channel.setParam ("compRatio", 8.0f);
        channel.setParam ("compAttack", 1.0f);
        channel.setParam ("compRelease", 100.0f);
        channel.setParam ("compDrive", 3.0f);
        channel.setParam ("compOutput", -3.0f);
        channel.setParam ("compColor", 1.0f);
        channel.setParam ("compFeedback", 0.15f);

        auto buffer = makeBuffer();
        channel.process (buffer, buffer.getNumSamples());

        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < buffer.getNumSamples(); ++i)
                REQUIRE (std::isfinite (buffer.getSample (c, i)));

        magnitudes[static_cast<size_t> (type)] =
            buffer.getMagnitude (0, buffer.getNumSamples() - blockSize, blockSize);
        REQUIRE (magnitudes[static_cast<size_t> (type)] > 0.001f);
        REQUIRE (magnitudes[static_cast<size_t> (type)] < 2.0f);
    }

    // The coloured characters must not all collapse to the same transfer.
    REQUIRE (magnitudes[1] != magnitudes[6]);

    // Colour stage: FET with colour on differs from FET with colour off.
    nc::MixRackChannel plain;
    plain.prepare (sampleRate, blockSize);
    plain.setParam ("hpfOn", 0.0f);
    plain.setParam ("lpfOn", 0.0f);
    plain.setParam ("eqOn", 0.0f);
    plain.setParam ("compOn", 1.0f);
    plain.setParam ("compType", 1.0f);
    plain.setParam ("compThresh", -30.0f);
    plain.setParam ("compRatio", 8.0f);
    plain.setParam ("compAttack", 1.0f);
    plain.setParam ("compRelease", 100.0f);
    plain.setParam ("compColor", 1.0f);

    nc::MixRackChannel bare;
    bare.prepare (sampleRate, blockSize);
    bare.setParam ("hpfOn", 0.0f);
    bare.setParam ("lpfOn", 0.0f);
    bare.setParam ("eqOn", 0.0f);
    bare.setParam ("compOn", 1.0f);
    bare.setParam ("compType", 1.0f);
    bare.setParam ("compThresh", -30.0f);
    bare.setParam ("compRatio", 8.0f);
    bare.setParam ("compAttack", 1.0f);
    bare.setParam ("compRelease", 100.0f);
    bare.setParam ("compColor", 0.0f);

    auto coloured = makeBuffer();
    auto uncoloured = makeBuffer();
    plain.process (coloured, coloured.getNumSamples());
    bare.process (uncoloured, uncoloured.getNumSamples());

    bool differs = false;
    for (int i = 0; i < coloured.getNumSamples(); ++i)
        differs = differs || std::abs (coloured.getSample (0, i) - uncoloured.getSample (0, i)) > 1.0e-5f;

    REQUIRE (differs);
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
