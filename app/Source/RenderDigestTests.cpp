/*
    Refactor safety net: one fixed session, hashed sample for sample.

    Nothing here tests a feature. The point is to pin the exact output of the
    block path before AudioEngine.cpp is split into translation units, so every
    later move can prove it changed no sample. The digest is captured from the
    current code and asserted forever after.

    Determinism comes from three things:
      - the demo assets are generated from a seeded juce::Random
        (0x4e430000 + track * 7919), so the source material is identical on
        every run;
      - the transport is reset and started by the test, not by a device;
      - the first blocks are discarded. The fader and master smoothers need
        ~16k samples to converge, and the aux tails decay exponentially, so
        hashing from the first block would depend on what the constructor's
        device did before the callback was detached.
*/

#include <catch2/catch_test_macros.hpp>

#include "AudioEngine.h"

#include <bit>
#include <cstdint>
#include <utility>
#include <vector>

namespace
{
constexpr int kBlockSize = 512;
constexpr int kWarmupBlocks = 32;   // never hashed: lets every smoother converge
constexpr int kHashedBlocks = 96;   // 1.024 s at 48 kHz
constexpr int kSessionTracks = 8;
constexpr int kClipTracks = 4;

std::uint64_t digestOf (nc::AudioEngine& engine)
{
    std::uint64_t hash = 0xcbf29ce484222325ull;   // FNV-1a 64 offset basis
    constexpr std::uint64_t prime = 0x100000001b3ull;

    std::vector<float> inputLeft (kBlockSize, 0.0f);
    std::vector<float> inputRight (kBlockSize, 0.0f);
    juce::AudioBuffer<float> out (2, kBlockSize);

    const float* inputs[2] { inputLeft.data(), inputRight.data() };
    float* outputs[2] { out.getWritePointer (0), out.getWritePointer (1) };

    for (int block = 0; block < kWarmupBlocks + kHashedBlocks; ++block)
    {
        out.clear();
        engine.processBlockForTesting (inputs, 2, outputs, 2, kBlockSize);

        if (block < kWarmupBlocks)
            continue;

        for (int channel = 0; channel < 2; ++channel)
            for (int i = 0; i < kBlockSize; ++i)
            {
                hash ^= std::bit_cast<std::uint32_t> (out.getSample (channel, i));
                hash *= prime;
            }
    }

    return hash;
}
} // namespace

TEST_CASE ("engine render digest is stable", "[digest]")
{
    nc::AudioEngine engine;
    engine.setBpm (128.0);
    engine.setActiveTrackCount (kSessionTracks);
    engine.setMetronome (false);
    engine.setLoopEnabled (false);

    // Let the constructor's device open (which prepares the render scratch),
    // then take the callback away so nothing renders behind the test.
    juce::Thread::sleep (300);
    engine.suspendAudioCallbackForTesting();

    std::vector<nc::AudioEngine::TimelineClip> clips;
    for (int track = 0; track < kClipTracks; ++track)
    {
        nc::AudioEngine::TimelineClip clip;
        clip.assetId = engine.demoAssetId (track);
        clip.track = track;
        clip.startBar = 1.0;
        clip.lengthBars = 4.0;
        clips.push_back (clip);
    }

    REQUIRE (clips.front().assetId >= 0);
    REQUIRE (engine.setTimelineClips (std::move (clips)) == 0);

    engine.setTrackGain (0, 0.7f);
    engine.setTrackPan (1, -0.4f);
    engine.setTrackSendA (0, 0.5f);
    engine.setTrackSendB (2, 0.25f);
    engine.setTrackWidth (3, 0.75f);
    engine.setMasterGain (0.8f);

    nc::AutomationLane lane;
    lane.addPoint (0.0, 0.25f);
    lane.addPoint (16.0, 0.9f);
    engine.setTrackAutomation (0, nc::AutomationParam::Volume, lane);

    engine.resetPosition();
    engine.setPlaying (true);

    // The digest is captured from the current code on the first run and pinned
    // here; every later refactor step must reproduce it exactly.
    const auto digest = digestOf (engine);
    INFO ("render digest: "
          << juce::String::formatted ("0x%016llx", static_cast<unsigned long long> (digest)));
    REQUIRE (digest == 0x1b5398dd4a9f297aull);
}
